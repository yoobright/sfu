#!/usr/bin/env python3
"""Generate the shared 128-entry TANH/SIGMOID quadratic LUT.

The table approximates tanh(x) on the positive half-axis [0, 8).  All region
widths are powers of two, so range reduction needs only comparisons, subtracts,
and bit slices:

* [0, 1): 64 intervals of width 1/64
* [1, 4): 48 intervals of width 1/16
* [4, 8): 16 intervals of width 1/4

SIGMOID addresses the same table with |x|/2 and reconstructs
(1 +/- tanh(|x|/2))/2 in the compose stage.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from scipy.optimize import linprog


LOCAL_VALUES = 1 << 16
C0_SCALE = 2.0**-26
C1_SCALE = 2.0**-21
C2_SCALE = 2.0**-21
XL = np.arange(LOCAL_VALUES, dtype=np.int64)
XL2 = (XL * XL >> 17) & 0x7FFF
T = XL.astype(np.float64) / LOCAL_VALUES


def segment_geometry(segment: int) -> tuple[float, float]:
    if segment < 64:
        return segment / 64.0, 1.0 / 64.0
    if segment < 112:
        return 1.0 + (segment - 64) / 16.0, 1.0 / 16.0
    return 4.0 + (segment - 112) / 4.0, 1.0 / 4.0


def minimax_quadratic(segment: int) -> np.ndarray:
    start, width = segment_geometry(segment)
    local = np.linspace(0.0, 1.0, 2049)
    values = np.tanh(start + width * local)
    basis = np.column_stack((np.ones_like(local), local, local * local))
    count = local.size
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


def fixed_result(coeffs: tuple[int, int, int]) -> np.ndarray:
    c0, c1, c2 = coeffs
    return c0 + (c1 * XL >> 11) - ((c2 * XL2 + 0x3FF) >> 10)


def evaluate_segment(segment: int, coeffs: tuple[int, int, int]) -> float:
    start, width = segment_geometry(segment)
    reference = np.tanh(start + width * T)
    return float(np.max(np.abs(fixed_result(coeffs) / float(1 << 26) - reference)))


def quantize_and_compensate(segment: int, polynomial: np.ndarray) -> tuple[int, int, int]:
    coeffs = [
        round(polynomial[0] / C0_SCALE),
        round(polynomial[1] / C1_SCALE),
        round(-polynomial[2] / C2_SCALE),
    ]
    limits = (1 << 26, 1 << 16, 1 << 12)
    coeffs = [min(max(value, 0), limit - 1) for value, limit in zip(coeffs, limits)]
    if segment == 0:
        coeffs[0] = 0

    best_error = evaluate_segment(segment, tuple(coeffs))
    for _ in range(3):
        improved = False
        for coefficient in range(3):
            if segment == 0 and coefficient == 0:
                continue
            base = coeffs[coefficient]
            best_value = base
            for delta in range(-12, 13):
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
    return tuple(coeffs)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "lut" / "tanh-coeffs.txt",
    )
    args = parser.parse_args()

    all_coeffs = []
    maximum_error = 0.0
    for segment in range(128):
        coeffs = quantize_and_compensate(segment, minimax_quadratic(segment))
        all_coeffs.append(coeffs)
        maximum_error = max(maximum_error, evaluate_segment(segment, coeffs))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii") as output:
        for c0, c1, c2 in all_coeffs:
            output.write(f"{c0:X} {c1:X} {c2:X}\n")

    print(f"wrote {len(all_coeffs)} segments to {args.output}")
    print(f"maximum exhaustive absolute error on [0, 8): {maximum_error:.9e}")


if __name__ == "__main__":
    main()
