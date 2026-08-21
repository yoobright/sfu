#!/usr/bin/env python3
"""Generate 64- or 128-entry sigmoid LUTs for the shared SFU pipeline.

The construction follows Oberman and Siu's enhanced-minimax workflow:
compute a degree-2 minimax approximation per segment, quantize the finite-word
coefficients, compensate the quantization with a local integer search, and
exhaustively evaluate every 16-bit local argument against the bit-accurate
datapath truncations.  The 128-entry RTL layout uses SIN-like power-of-two
region folding.  The 64-entry area experiment reallocates entries toward the
worst-error [2, 4) region:

* 64 entries:  32 x 1/16 on [0, 2), 20 x 1/10 on [2, 4),
               then 12 x 1/6 on [4, 6)
* 128 entries: 64 x 1/32 on [0, 2), then 64 x 1/16 on [2, 6)
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from scipy.optimize import linprog


LOCAL_VALUES = 1 << 16
C0_SCALE = 2.0**-26
# The polynomial variable is the normalized local coordinate t in [0, 1).
# These scales exactly match the shared multiplier alignment for a 16-bit t.
C1_SCALE = 2.0**-21
XL = np.arange(LOCAL_VALUES, dtype=np.int64)
XL2 = (XL * XL >> 17) & 0x7FFF
T = XL.astype(np.float64) / LOCAL_VALUES


def c2_scale(num_segments: int) -> float:
    return 2.0 ** (-22 if num_segments == 64 else -24)


def c2_shift(num_segments: int) -> int:
    return 11 if num_segments == 64 else 13


def sigmoid_negative_abs(x: np.ndarray) -> np.ndarray:
    """Return sigmoid(-x) for non-negative x."""
    return 1.0 / (1.0 + np.exp(x))


def segment_geometry(segment: int, num_segments: int) -> tuple[float, float]:
    """Return the start and width of a sigmoid interval."""
    if num_segments == 64:
        if segment < 32:
            return segment / 16.0, 1.0 / 16.0
        if segment < 52:
            return 2.0 + (segment - 32) / 10.0, 1.0 / 10.0
        return 4.0 + (segment - 52) / 6.0, 1.0 / 6.0

    half = num_segments // 2
    if segment < half:
        width = 2.0 / half
        return segment * width, width
    width = 4.0 / half
    return 2.0 + (segment - half) * width, width


def minimax_quadratic(segment: int, num_segments: int) -> np.ndarray:
    start, width = segment_geometry(segment, num_segments)
    local = np.linspace(0.0, 1.0, 2049)
    values = sigmoid_negative_abs(start + width * local)
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


def evaluate_segment(
    segment: int, num_segments: int, coeffs: tuple[int, int, int]
) -> float:
    c0, c1, c2 = coeffs
    # Arithmetic right shift of the negative C1 product rounds toward -inf.
    fixed = c0 - ((c1 * XL + 0x7FF) >> 11) + (
        c2 * XL2 >> c2_shift(num_segments)
    )
    start, width = segment_geometry(segment, num_segments)
    reference = sigmoid_negative_abs(start + width * T)
    return float(np.max(np.abs(fixed / float(1 << 26) - reference)))


def quantize_and_compensate(
    segment: int, num_segments: int, polynomial: np.ndarray
) -> tuple[int, int, int]:
    coeffs = [
        round(polynomial[0] / C0_SCALE),
        round(-polynomial[1] / C1_SCALE),
        round(polynomial[2] / c2_scale(num_segments)),
    ]
    limits = (1 << 26, 1 << 16, 1 << 12)
    coeffs = [min(max(value, 0), limit - 1) for value, limit in zip(coeffs, limits)]
    best_error = evaluate_segment(segment, num_segments, tuple(coeffs))

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
                error = evaluate_segment(segment, num_segments, tuple(candidate))
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
    num_segments: int,
) -> list[tuple[int, int, int]]:
    """Remove upward h(x) steps using fine C0-aware coefficient correction."""
    adjusted = [list(coeffs) for coeffs in all_coeffs]
    xl = LOCAL_VALUES - 1
    xl2 = (xl * xl >> 17) & 0x7FFF

    def endpoint(coeffs: list[int]) -> int:
        c0, c1, c2 = coeffs
        return c0 - ((c1 * xl + 0x7FF) >> 11) + (
            c2 * xl2 >> c2_shift(num_segments)
        )

    # Work backwards because changing C0 also changes the target seen by the
    # preceding interval.  C0 provides one-Q0.26-LSB boundary resolution;
    # the old C1-only correction moved an endpoint by roughly 32 such LSBs.
    for segment in reversed(range(num_segments - 1)):
        current = adjusted[segment]
        target = adjusted[segment + 1][0]
        if endpoint(current) >= target:
            continue

        candidates = []
        for delta_c1 in range(-2, 3):
            for delta_c2 in range(-8, 9):
                candidate = [
                    current[0],
                    current[1] + delta_c1,
                    current[2] + delta_c2,
                ]
                if not (0 <= candidate[1] < 1 << 16):
                    continue
                if not (0 <= candidate[2] < 1 << 12):
                    continue
                required_c0 = target - endpoint(candidate)
                for delta_c0 in {required_c0, max(required_c0, 0)}:
                    trial = candidate.copy()
                    trial[0] += delta_c0
                    c0_limit = (1 << 25) if segment == 0 else (1 << 26) - 1
                    if not 0 <= trial[0] <= c0_limit:
                        continue
                    if endpoint(trial) < target:
                        continue
                    cost = (
                        abs(delta_c0)
                        + 32 * abs(delta_c1)
                        + 4 * abs(delta_c2)
                        + endpoint(trial) - target
                    )
                    candidates.append((cost, trial))

        if not candidates:
            raise RuntimeError(f"cannot make sigmoid segment {segment} continuous")

        # Evaluate only the best structural candidates with the exact datapath.
        candidates.sort(key=lambda item: item[0])
        adjusted[segment] = min(
            (candidate for _, candidate in candidates[:24]),
            key=lambda candidate: evaluate_segment(
                segment, num_segments, tuple(candidate)
            ),
        )

    return [tuple(coeffs) for coeffs in adjusted]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--segments", type=int, choices=(64, 128), default=128)
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
    )
    args = parser.parse_args()
    if args.output is None:
        filename = "sigmoid-64-coeffs.txt" if args.segments == 64 else "sigmoid-coeffs.txt"
        args.output = Path(__file__).resolve().parents[1] / "lut" / filename

    all_coeffs = []
    maximum_error = 0.0
    for segment in range(args.segments):
        coeffs = quantize_and_compensate(
            segment, args.segments, minimax_quadratic(segment, args.segments)
        )
        all_coeffs.append(coeffs)

    all_coeffs = enforce_boundary_monotonicity(all_coeffs, args.segments)
    for segment, coeffs in enumerate(all_coeffs):
        maximum_error = max(
            maximum_error, evaluate_segment(segment, args.segments, coeffs)
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii") as output:
        for c0, c1, c2 in all_coeffs:
            output.write(f"{c0:X} {c1:X} {c2:X}\n")

    print(f"wrote {len(all_coeffs)} segments to {args.output}")
    print(f"maximum exhaustive absolute error on [0, 6): {maximum_error:.9e}")


if __name__ == "__main__":
    main()
