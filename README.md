# SFU - Special Function Unit

A high-performance hardware implementation of special mathematical functions for single-precision floating-point numbers (FP32), designed using Chisel HDL.

## Overview

This project implements a unified **Special Function Unit (SFU)** that supports ten FP32 functions using a shared pipelined architecture based on fixed-point quadratic interpolation with lookup tables (LUT):

- **EXP2**: Base-2 exponential $2^x$
- **LOG2**: Base-2 logarithm $\log_2(x)$
- **RCP**: Reciprocal $1/x$
- **SQRT**: Square root $\sqrt{x}$
- **RSQRT**: Reciprocal square root $1/\sqrt{x}$
- **SIN**: $\sin\!\left(\frac{\pi}{2} x\right)$
- **COS**: $\cos\!\left(\frac{\pi}{2} x\right)$
- **SIGMOID**: $\sigma(x)=1/(1+e^{-x})$
- **EXP**: Natural exponential $e^x$ over the full FP32 input domain
- **TANH**: Hyperbolic tangent $\tanh(x)$, effective over $[-8,8]$

The architecture is based on the multifunction interpolation approach described by Oberman and Siu \[1\], which is consistent with the design used in NVIDIA SFUs.

## Algorithm

Each function is approximated by a **quadratic polynomial** over a set of small sub-intervals. Given an input $x$:

$$
\begin{equation}
\begin{aligned}
x        &= 2^{E} \times (1 + M), \quad M = M_{high} + M_{low} \\
\\
\text{index} &= M_{high} \quad \text{(high bits of mantissa, 6–7 bits)} \\
\\
x_l      &= M_{low} \quad \text{(low bits of mantissa, fixed-point)} \\
\\
f(x)     &\approx c_0 + c_1 \cdot x_l + c_2 \cdot x_l^2
\end{aligned}
\end{equation}
$$

The three coefficients $(c_0, c_1, c_2)$ are stored per interval in a LUT and optimized offline to minimize the maximum approximation error (minimax optimization). All arithmetic in the polynomial evaluation is performed in **fixed-point integer arithmetic**, avoiding floating-point operations in the critical path.

For **EXP2**, the argument is decomposed as $x = I + F$ where $I = \lfloor x \rfloor$ is the integer part and $F$ is the fractional part. The polynomial approximates $2^F$ over $[0, 1)$, and the exponent $I$ is applied in the compose stage.

For **EXP**, the range reducer first computes $y=x\log_2(e)$ with the Q1.31
constant `0xB8AA3B29`, then decomposes $y=I+F$ and reuses the EXP2 LUT,
polynomial, and compose path:

$$e^x=2^{x\log_2(e)}=2^I2^F.$$

EXP accepts all FP32 inputs without an application-specific clipping interval.
Both signs use $I=\lfloor y\rfloor$ and $F=y-I\in[0,1)$. The full 24-bit
significand is multiplied before rounding $y$ to Q23, avoiding early truncation
of small inputs. The exponent metadata is signed 9-bit (stored as `int16_t`
in the C model) to cover $I=-150$ through $127$.
Normal outputs retain the EXP2 polynomial's truncating mantissa conversion;
EXP additionally handles a carry into $2.0$ and rounds subnormal outputs to
nearest, ties to even. No additional LUT is required.

| FP32 input | EXP result |
|------------|------------|
| NaN | NaN |
| $+\infty$ / $-\infty$ | $+\infty$ / $+0$ |
| $+0$ / $-0$ | $1$ |
| $x\ge 88.72283935546875$ (`0x42B17218`) | $+\infty$ (FP32 overflow) |
| $x\le -103.97208404541015625$ (`0xC2CFF1B5`) | $+0$ (rounds below minimum subnormal) |
| Other finite inputs | Approximation to $e^x$, including subnormal outputs |

These are FP32 representability limits, not user-selected clipping limits.
EXP remains an approximate SFU operation, not a correctly rounded `expf`.

For **SIN/COS**, the functions computed are $\sin(\frac{\pi}{2} x)$ and $\cos(\frac{\pi}{2} x)$. The period is 4, so `quadrant = floor(x) mod 4` is read from the two low bits of the integer part of $x$. The LUT stores coefficients for $\sin(\frac{\pi}{2} t)$ over $t \in [0, 1)$ with 64 sub-intervals. The fractional part $f \in [0, 1)$ is used for interpolation, and the quadrant determines whether to use $t = f$ or $t = 1 - f$ and the sign of the result.

For **TANH/SIGMOID**, one 128-entry LUT approximates $\tanh(|x|)$ in unsigned
Q0.26. TANH applies odd symmetry. SIGMOID reuses the same range reducer,
polynomial, and LUT by addressing it with $|x|/2$, then composing
$\sigma(x)=(1+\tanh(x/2))/2$. Both operations saturate at $|x|\ge8$.
For $|x|<2^{-11}$, TANH directly returns the FP32 input using
$\tanh(x)\approx x$; this preserves `±0` and subnormals and avoids magnifying
the fixed Q0.26 output step into thousands of ULPs near zero.

The table uses power-of-two interval widths: 64 × $1/64$ over $[0,1)$,
48 × $1/16$ over $[1,4)$, and 16 × $1/4$ over $[4,8)$. Coefficients come
from a degree-2 minimax solve followed by finite-word compensation and an
exhaustive check of all 65,536 local fixed-point arguments per interval. Run
`python3 tools/gen_tanh_lut.py` to reproduce it (NumPy and SciPy required).

The final result is assembled by combining the polynomial output with the input exponent according to each function's composition rule.

## Hardware Design

### Pipeline Structure

```
S0: Filter (1 cycle)
    - Handle special values: NaN, ±Inf, ±0, subnormals (treated as zero)
    - Handle out-of-range inputs for EXP2 (overflow/underflow)
    - Handle EXP FP32 overflow/underflow, preserving subnormal outputs
    - Saturate TANH/SIGMOID when |x| >= 8
    - Bypass TANH with the original input when |x| < 2^-11
    - Handle negative inputs for LOG2, SQRT, RSQRT
    - Output bypass flag and bypass value for special cases
    - Pass normal inputs to next stage

S1: RangeReduce (1 cycle)
    - EXP2/SIN/COS: Compute integer and fractional decomposition
    - EXP: multiply the magnitude by Q1.31 log2(e), then use the EXP2 decomposition
    - SIN/COS: quadrant = floor(x) mod 4; map fractional part f to t = f or 1-f based on quadrant[0]; sign from quadrant[1]
    - TANH/SIGMOID: reduce |x| or |x|/2 into the shared 128-entry TANH table
    - LOG2/RCP/SQRT/RSQRT: Extract exponent and split mantissa into index + xl
    - SQRT/RSQRT: index[6] = E mod 2 (selects even/odd LUT)
    - Output: index (7 bits), xl (17 bits), exp (9 bits signed), sign (1 bit)

S2: LUT (1 cycle)
    - Index: 7 bits → 128 entries per function
    - EXP2/EXP/LOG2/SIN/COS: 64-entry tables (index[5:0]); EXP shares EXP2
    - RCP/TANH: 128-entry tables (full 7-bit index); SIGMOID shares TANH
    - SQRT/RSQRT: two 64-entry tables (even/odd), selected by index[6]
    - Each entry stores (c0, c1, c2) as signed fixed-point integers

S3: Poly Stage 1 — Squarer (1 cycle)
    - Compute xl^2 (17×17 unsigned multiply)
    - Truncate and shift to 15-bit aligned representation

S4: Poly Stage 2 — Parallel Multiply (1 cycle)
    - Compute c1 × xl  (17×17 signed multiply → 35 bits)
    - Compute c2 × xl² (13×15 signed multiply → 29 bits)

S5: Poly Stage 3 — Align and Sum (1 cycle)
    - Align c1·xl and c2·xl² to the scale of c0
    - Compute polyResult = c0 + aligned(c1·xl) + aligned(c2·xl²)
    - Output: 27-bit fixed-point result

S6: Compose (1 cycle)
    - Assemble final FP32 from polyResult and exp:
      · EXP2:  exp_out = 127 + exp,  mant = polyResult[24:2]
      · EXP:   EXP2-style compose with carry normalization and gradual underflow
      · LOG2:  leading-zero detection on {exp, polyResult[25:0]}, normalize
      · RCP:   exp_out = 126 - exp,  mant = polyResult[24:2]
      · SQRT:  exp_out = 127 + exp,  mant = polyResult[24:2]
      · RSQRT: exp_out = 126 - exp,  mant = polyResult[24:2]
      · SIN/COS: leading-zero normalization; clamp to ±1 if polyResult overflows
      · TANH: apply input sign and normalize the Q0.26 magnitude
      · SIGMOID: form $(1\pm\mathrm{TANH})/2$ and normalize Q0.26
    - If bypass flag set, output bypassVal; otherwise output assembled result
```

### Key Features

- **Unified Architecture**: Single 7-stage pipeline serves all ten functions
- **Full Throughput**: One result per cycle after initial latency
- **128-Entry LUTs**: Per-function coefficient tables (some split into even/odd for SQRT/RSQRT)
- **Fixed-Point Polynomial**: Quadratic approximation in integer arithmetic — no FP multipliers in datapath
- **Offline Coefficient Optimization**: Coefficients minimized via minimax optimization (optimizer tool)
- **Special Value Handling**: Comprehensive coverage of IEEE 754 edge cases

## Accuracy

Coefficients are optimized offline using the `optimizer` tool to minimize the worst-case absolute error within each sub-interval. The following tables compare accuracy against NVIDIA GPU results (measured with `-use_fast_math`) across $[0.25, 4)$.

### EXP2

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 2.384e-07 | **2** | 4.314e-08 | 0.36 |
| | NVIDIA GPU | 1.192e-07 | 1 | 3.020e-08 | 0.25 |
| **[0.5, 1)** | This work | 2.384e-07 | **2** | 4.269e-08 | 0.36 |
| | NVIDIA GPU | 1.192e-07 | 1 | 3.746e-08 | 0.31 |
| **[1, 2)** | This work | 2.384e-07 | **1** | 2.544e-08 | 0.11 |
| | NVIDIA GPU | 2.384e-07 | 1 | 9.273e-08 | 0.39 |
| **[2, 4)** | This work | 9.537e-07 | **1** | 7.624e-08 | 0.11 |
| | NVIDIA GPU | 9.537e-07 | 1 | 2.801e-07 | 0.39 |

### LOG2

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 2.384e-07 | 2 | 6.518e-08 | 0.55 |
| | NVIDIA GPU | 2.384e-07 | 2 | 1.201e-07 | 1.01 |
| **[0.5, 1)** | This work | 1.192e-07 | 1.6M | 2.775e-08 | 3.43 |
| | NVIDIA GPU | 2.384e-07 | 24M | 7.843e-08 | 20.06 |
| **[1, 2)** | This work | 8.941e-08 | 866M | 1.802e-08 | 109.79 |
| | NVIDIA GPU | 1.602e-07 | 6.1M | 4.342e-08 | 11.20 |
| **[2, 4)** | This work | 2.384e-07 | **2** | 3.972e-08 | 0.33 |
| | NVIDIA GPU | 1.192e-07 | 1 | 2.954e-08 | 0.25 |

> **Note on LOG2 ULP near 1.0**: Very large ULP values in $[0.5, 2)$ are expected. Near $x = 1$, $\log_2(x) \approx 0$, so any small absolute error maps to an enormous ULP count. The absolute errors remain below $2^{-22}$ throughout.

### RCP

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 2.384e-07 | **1** | 2.183e-08 | 0.09 |
| | NVIDIA GPU | 2.384e-07 | 1 | 3.151e-08 | 0.13 |
| **[0.5, 1)** | This work | 1.192e-07 | **1** | 1.092e-08 | 0.09 |
| | NVIDIA GPU | 1.192e-07 | 1 | 1.575e-08 | 0.13 |
| **[1, 2)** | This work | 5.960e-08 | **1** | 5.459e-09 | 0.09 |
| | NVIDIA GPU | 5.960e-08 | 1 | 7.877e-09 | 0.13 |
| **[2, 4)** | This work | 2.980e-08 | **1** | 2.729e-09 | 0.09 |
| | NVIDIA GPU | 2.980e-08 | 1 | 3.938e-09 | 0.13 |

### SQRT

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 5.960e-08 | **1** | 4.992e-09 | 0.08 |
| | NVIDIA GPU | 5.960e-08 | 1 | 9.946e-09 | 0.17 |
| **[0.5, 1)** | This work | 5.960e-08 | **1** | 4.900e-09 | 0.08 |
| | NVIDIA GPU | 5.960e-08 | 1 | 1.016e-08 | 0.17 |
| **[1, 2)** | This work | 1.192e-07 | **1** | 9.985e-09 | 0.08 |
| | NVIDIA GPU | 1.192e-07 | 1 | 1.989e-08 | 0.17 |
| **[2, 4)** | This work | 1.192e-07 | **1** | 9.801e-09 | 0.08 |
| | NVIDIA GPU | 1.192e-07 | 1 | 2.032e-08 | 0.17 |

### RSQRT

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 1.192e-07 | **1** | 1.692e-08 | 0.14 |
| | NVIDIA GPU | 2.384e-07 | 2 | 2.677e-08 | 0.22 |
| **[0.5, 1)** | This work | 1.192e-07 | **1** | 1.388e-08 | 0.12 |
| | NVIDIA GPU | 1.192e-07 | 1 | 2.488e-08 | 0.21 |
| **[1, 2)** | This work | 5.960e-08 | **1** | 8.461e-09 | 0.14 |
| | NVIDIA GPU | 1.192e-07 | 2 | 1.338e-08 | 0.22 |
| **[2, 4)** | This work | 5.960e-08 | **1** | 6.942e-09 | 0.12 |
| | NVIDIA GPU | 5.960e-08 | 1 | 1.244e-08 | 0.21 |

### SIN

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 2.980e-07 | 12 | 1.068e-07 | 3.61 |
| | NVIDIA GPU | 3.278e-07 | 11 | 1.195e-07 | 4.03 |
| **[0.5, 1)** | This work | 2.980e-07 | **10** | 1.019e-07 | 1.80 |
| | NVIDIA GPU | 3.576e-07 | 10 | 1.140e-07 | 2.00 |
| **[1, 2)** | This work | 2.384e-07 | **4** | 5.998e-08 | 1.01 |
| | NVIDIA GPU | 2.384e-07 | 4 | 4.592e-08 | 0.77 |
| **[2, 4)** | This work | 4.470e-07 | 6.4M | 1.214e-07 | 18.70 |
| | NVIDIA GPU | 6.557e-07 | 884M | 1.678e-07 | 242.62 |

### COS

| Interval | Implementation | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|---------------|-----------|--------|-----------|--------|
| **[0.25, 0.5)** | This work | 1.788e-07 | **3** | 6.779e-08 | 1.14 |
| | NVIDIA GPU | 2.384e-07 | 4 | 4.643e-08 | 0.78 |
| **[0.5, 1)** | This work | 2.980e-07 | **5** | 1.024e-07 | 1.72 |
| | NVIDIA GPU | 2.980e-07 | 5 | 7.581e-08 | 1.27 |
| **[1, 2)** | This work | 2.980e-07 | 8.7M | 1.017e-07 | 28.40 |
| | NVIDIA GPU | 4.023e-07 | 875M | 1.188e-07 | 347.30 |
| **[2, 4)** | This work | 2.980e-07 | **9** | 7.467e-08 | 1.33 |
| | NVIDIA GPU | 4.172e-07 | 13 | 9.454e-08 | 1.72 |

> **Note on SIN/COS ULP near zeros**: Large ULP values near $\sin(x) = 0$ or $\cos(x) = 0$ are expected for the same reason as LOG2 near 1. The absolute errors remain small.

### SIGMOID

The C model is compared with the FP32 rounding of a double-precision
$\frac{1+\tanh(x/2)}{2}$ reference. Each interval contains 1,048,576 uniformly
spaced samples.

| Interval | MaxAbsErr | MaxULP |
|----------|-----------|--------|
| **[0, 0.5)** | 2.384186e-07 | 4 |
| **[0.5, 1)** | 1.788139e-07 | 3 |
| **[1, 2)** | 1.192093e-07 | 2 |
| **[2, 4)** | **5.960464e-07** | **10** |
| **[4, 6)** | 2.384186e-07 | 4 |
| **[6, 8)** | 1.788139e-07 | 3 |

The shared-path regression also verifies that `SIGMOID(x)` and `TANH(x/2)`
produce identical LUT indices and local interpolation arguments. Their final
FP32 results differ by at most 5.960464e-08 (one ULP around 0.5), due only to
the order of fixed-point composition and FP32 normalization. The maximum
quantization-induced monotonic reversal is 1.788139e-07.

Run `make accuracy-sigmoid` for the interval table and `make test-cmodel` for
the shared-path, special-value, saturation, and monotonicity checks.

### TANH

The coefficient generator's exhaustive fixed-point check reports a worst-case
absolute error of $9.274\times10^{-7}$ on $[0,8)$. The FP32 C-model interval
test reports the following results after input reduction and output
normalization. Each interval contains 1,048,576 uniformly spaced samples.

| Interval | MaxAbsErr | MaxULP | WorstULPX |
|----------|-----------|--------|-----------|
| **[0, 1)** | 2.384186e-07 | **237** | 1.317549e-02 |
| **[1, 2)** | **1.132488e-06** | 19 | 1.075612e+00 |
| **[2, 4)** | 3.576279e-07 | 6 | 2.000003e+00 |
| **[4, 6)** | 4.172325e-07 | 7 | 4.047477e+00 |
| **[6, 8)** | 2.384186e-07 | 4 | 7.488652e+00 |

The full-domain `[-8,8]` regression samples 4,194,305 points and reports a
maximum absolute error of 9.70154e-07 and a maximum quantization-induced
monotonic reversal of 3.57628e-07. The $|x|<2^{-11}$ bypass reduces the
`[0,1)` result from 2048 to 237 ULP. The remaining worst ULP occurs near
$x=0.0131755$, outside the bypass region, where an absolute error of
$2.20723\times10^{-7}$ spans 237 FP32 ULPs. Thus ULP is reported for
completeness but is not a stable TANH error metric near zero; absolute error
remains the primary bound. The regression explicitly checks positive and
negative values immediately below $2^{-11}$, the exact boundary, subnormals,
`±0`, `±Inf`, NaN, and the `±8` saturation boundaries.

Run `make accuracy-tanh` for the interval table and `make test-cmodel` for the
full-domain and special-value regression.

### EXP

The EXP accuracy test samples 1,048,576 points in each interval. The reference
is `exp` evaluated in double precision and rounded to FP32.

| Interval | MaxAbsErr | MaxULP | AvgAbsErr | AvgULP |
|----------|-----------|--------|-----------|--------|
| [-104, -87.33655) | 1.401298e-45 | **1** | 2.994966e-47 | 0.02 |
| [-87.33655, -16) | 7.105427e-15 | **2** | 3.502094e-17 | 0.27 |
| [-16, -8) | 2.910383e-11 | **2** | 9.368642e-13 | 0.27 |
| [-8, -4) | 1.862645e-09 | **2** | 1.008709e-10 | 0.27 |
| [-4, -2) | 1.490116e-08 | **2** | 1.306388e-09 | 0.27 |
| [-2, -1) | 5.960464e-08 | **2** | 5.250527e-09 | 0.26 |
| [-1, 0) | 1.192093e-07 | **2** | 1.400687e-08 | 0.28 |
| [0, 16) | 1.000000e+00 | **2** | 1.240312e-02 | 0.27 |
| [16, 64) | 5.902958e+20 | **2** | 3.000142e+18 | 0.27 |
| [64, 88.72283172607422] | 4.056482e+31 | **2** | 3.277481e+29 | 0.28 |

Run `make accuracy-exp` to reproduce the C-model table. The default EXP input
generator spans the finite, nonzero output range. `make test-cmodel` checks
4,194,305 uniformly spaced inputs for monotonicity and the 2-ULP sampled bound,
plus adjacent floats at range-reduction boundaries, the old clipping limits,
overflow/underflow, signed zeros, subnormal inputs, NaN and infinities.
A further 1,048,576 deterministic raw-bit samples cover tiny inputs and tails.
These are sampled results, not an exhaustive FP32 accuracy proof. CPU, GPU,
and MPFR references now evaluate unrestricted exponential.

`make test-exp-rtl` regenerates RTL and compares EXP bit-for-bit with the C
model using the same regression inputs (requires Mill/Chisel dependencies and
Verilator). Chisel source includes the full-range EXP path. The checked-in
`chisel/generated/SFU.sv` has **not yet been regenerated for this change**:
Maven dependency downloads were unavailable in the authoring environment.
The Chisel build now always elaborates before simulation to avoid silently
using that older generated artifact. RTL elaboration/equivalence and GPU/MPFR
build validation remain pending; the accuracy table above is C-model only.

### NVIDIA PTX ISA Specification Compliance

| Function | NVIDIA Spec | This Work (max over [0.5, 4)) | Status |
|----------|-------------|-------------------------------|--------|
| **EXP2** | Max 2 ULP | Max 2 ULP | **Satisfied** |
| **LOG2** | Max $2^{-22}$ abs err in $(0.5, 2)$ | Max 2.384e-07 ($\approx 2^{-22}$) | **Satisfied** |
| **RCP** | Max 1 ULP | Max 1 ULP | **Satisfied** |
| **SQRT** | Max rel err $2^{-23}$ | Max rel err 1.192e-07 ($\approx 2^{-23}$) | **Satisfied** |
| **RSQRT** | Max rel err $2^{-22.9}$ | Max rel err 1.192e-07 ($\approx 2^{-22.9}$) | **Satisfied** |

$2^{-22} \approx 2.38 \times 10^{-7}$, $2^{-23} \approx 1.19 \times 10^{-7}$

## Dependencies

### Required

- **Chisel 6.6.0**: Hardware description language
- **Scala 2.13.15**: Programming language for Chisel
- **Mill**: Build tool for Scala/Chisel projects
- **Verilator**: For simulation and verification

### Optional

- **CUDA/NVCC**: For GPU-accelerated reference implementation (NVIDIA GPU required)
- **Synopsys Design Compiler**: For ASIC synthesis

## Testing and Verification

### Simulation

Verilator-based testbench with:

- Test vector generation across full FP32 range $[0.25, 4)$ in sub-intervals
- Special value testing (NaN, Inf, zero, negative numbers, subnormals)
- ULP (Unit in Last Place) error measurement
- Waveform generation (FST format) for debugging

### Accuracy Metrics

- **ULP Error**: Floating-point accuracy in units of the last place
- **Relative Error**: Percentage deviation from reference value
- **Absolute Error**: Absolute difference from reference value

## References

\[1\] S. F. Oberman and M. Y. Siu, "A high-performance area-efficient multifunction interpolator," *17th IEEE Symposium on Computer Arithmetic (ARITH'05)*, Cape Cod, MA, USA, 2005, pp. 272–279, doi: [10.1109/ARITH.2005.7](https://doi.org/10.1109/ARITH.2005.7).
