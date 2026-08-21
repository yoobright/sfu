#!/usr/bin/env python3
"""Generate fixed-point sigmoid coefficients for the shared SFU pipeline.

The construction follows Oberman and Siu's enhanced-minimax workflow:
compute a degree-2 minimax approximation per segment, quantize the finite-word
coefficients, compensate the quantization with a local integer search, and
exhaustively evaluate every reduced Q0.23 input against the bit-accurate
datapath truncations.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from scipy.optimize import linprog


NUM_SEGMENTS = 128
LOCAL_VALUES = 1 << 16
C0_SCALE = 2.0**-26
C1_SCALE = 2.0**-13
C2_SCALE = 2.0**-8
XL = np.arange(LOCAL_VALUES, dtype=np.int64)
XL2 = (XL * XL >> 17) & 0x7FFF


def sigmoid_negative_abs(u: np.ndarray) -> np.ndarray:
    """Return sigmoid(-16*u) for u in [0, 1)."""
    return 1.0 / (1.0 + np.exp(16.0 * u))


def minimax_quadratic(segment: int) -> np.ndarray:
    local = np.linspace(0.0, 1.0 / NUM_SEGMENTS, 2049)
    values = sigmoid_negative_abs(segment / NUM_SEGMENTS + local)
    basis = np.column_stack((np.ones_like(local), local, local * local))
    count = local.size

    # Minimize E subject to -E <= P(x) - f(x) <= E.
    constraints = np.vstack(
        (
            np.column_stack((basis, -np.ones(count))),
            np.column_stack((-basis, -np.ones(count))),
        )
    )
    bounds = np.concatenate((values, -values))
    result = linprog(
        (0.0, 0.0, 0.0, 1.0),
        A_ub=constraints,
        b_ub=bounds,
        bounds=((None, None), (None, None), (None, None), (0.0, None)),
        method="highs",
    )
    if not result.success:
        raise RuntimeError(f"minimax solve failed for segment {segment}: {result.message}")
    return result.x[:3]


def evaluate_segment(segment: int, coeffs: tuple[int, int, int]) -> float:
    c0, c1, c2 = coeffs
    # Arithmetic right shift of the negative C1 product rounds toward -inf.
    fixed = c0 - ((c1 * XL + 0x3FF) >> 10) + (c2 * XL2 >> 11)
    reduced = (segment * LOCAL_VALUES + XL) / float(1 << 23)
    reference = sigmoid_negative_abs(reduced)
    return float(np.max(np.abs(fixed / float(1 << 26) - reference)))


def quantize_and_compensate(segment: int, polynomial: np.ndarray) -> tuple[int, int, int]:
    coeffs = [
        round(polynomial[0] / C0_SCALE),
        round(-polynomial[1] / C1_SCALE),
        round(polynomial[2] / C2_SCALE),
    ]
    limits = (1 << 26, 1 << 16, 1 << 12)
    coeffs = [min(max(value, 0), limit - 1) for value, limit in zip(coeffs, limits)]
    best_error = evaluate_segment(segment, tuple(coeffs))

    # Compensate C0/C1/C2 quantization and the pipeline's two truncating shifts.
    for _ in range(3):
        improved = False
        for coefficient in range(3):
            base = coeffs[coefficient]
            best_value = base
            for delta in range(-8, 9):
                candidate = coeffs.copy()
                candidate[coefficient] = base + delta
                if not 0 <= candidate[coefficient] < limits[coefficient]:
                    continue
                error = evaluate_segment(segment, tuple(candidate))
                if error < best_error:
                    best_error = error
                    best_value = candidate[coefficient]
            if best_value != base:
                coeffs[coefficient] = best_value
                improved = True
        if not improved:
            break

    # σ(+0) must not dip below 0.5 after symmetry reconstruction.  The
    # unconstrained minimax polynomial overshoots h(0)=0.5 by a few Q0.26 LSBs.
    if segment == 0:
        coeffs[0] = min(coeffs[0], 1 << 25)

    return tuple(coeffs)


def enforce_boundary_monotonicity(
    all_coeffs: list[tuple[int, int, int]],
) -> list[tuple[int, int, int]]:
    """Remove downward output steps at independently optimized boundaries."""
    adjusted = [list(coeffs) for coeffs in all_coeffs]
    xl = LOCAL_VALUES - 1
    xl2 = (xl * xl >> 17) & 0x7FFF

    def endpoint(coeffs: list[int]) -> int:
        c0, c1, c2 = coeffs
        return c0 - ((c1 * xl + 0x3FF) >> 10) + (c2 * xl2 >> 11)

    for segment in range(NUM_SEGMENTS - 1):
        current = adjusted[segment]
        target = adjusted[segment + 1][0]
        if endpoint(current) >= target:
            continue

        best = None
        for delta_c1 in range(-32, 9):
            for delta_c2 in range(-8, 33):
                candidate = [
                    current[0],
                    current[1] + delta_c1,
                    current[2] + delta_c2,
                ]
                if not (0 <= candidate[1] < 1 << 16):
                    continue
                if not (0 <= candidate[2] < 1 << 12):
                    continue
                candidate_end = endpoint(candidate)
                if candidate_end < target:
                    continue
                cost = (
                    delta_c1 * delta_c1
                    + delta_c2 * delta_c2
                    + 0.02 * (candidate_end - target)
                )
                if best is None or cost < best[0]:
                    best = (cost, candidate)

        if best is None:
            raise RuntimeError(f"cannot make sigmoid segment {segment} continuous")
        adjusted[segment] = best[1]

    return [tuple(coeffs) for coeffs in adjusted]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "lut" / "sigmoid-coeffs.txt",
    )
    args = parser.parse_args()

    all_coeffs = []
    maximum_error = 0.0
    for segment in range(NUM_SEGMENTS):
        coeffs = quantize_and_compensate(segment, minimax_quadratic(segment))
        all_coeffs.append(coeffs)

    all_coeffs = enforce_boundary_monotonicity(all_coeffs)
    for segment, coeffs in enumerate(all_coeffs):
        maximum_error = max(maximum_error, evaluate_segment(segment, coeffs))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii") as output:
        for c0, c1, c2 in all_coeffs:
            output.write(f"{c0:X} {c1:X} {c2:X}\n")

    print(f"wrote {len(all_coeffs)} segments to {args.output}")
    print(f"maximum exhaustive absolute error on [0, 16): {maximum_error:.9e}")


if __name__ == "__main__":
    main()
