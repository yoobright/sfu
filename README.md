# SFU - Special Function Unit

A high-performance hardware implementation of special mathematical functions for single-precision floating-point numbers (FP32), designed using Chisel HDL.

## Overview

This project implements a unified **Special Function Unit (SFU)** that supports multiple FP32 functions using a shared pipelined architecture combining lookup tables (LUT) and polynomial approximation:

- **EXP2**: Base-2 exponential $2^x$
- **LOG2**: Base-2 logarithm $\log_2(x)$
- **RCP**: Reciprocal $1/x$
- **SQRT**: Square root $\sqrt{x}$
- **RSQRT**: Reciprocal square root $1/\sqrt{x}$

This project reuses the **XiangShan Fudian** floating-point unit library for basic FP32 arithmetic operations (multiplication, fused multiply-add).

## Algorithm

$$
\begin{equation}
\begin{aligned}
f                    &= \text{function}(x) \\
\\
x                    &= 2^{E} \times (1 + M) \\
\\
M                    &= M_{high} + M_{low} \\
\\
\text{Value}_1       &= \text{LUT}[M_{high}] \\
\\
T                    &= \frac{1}{1 + M_{high}} = \text{LUT}[M_{high}] \\
\\
r                    &= M_{low} \times T \\
\\
\text{poly}(r)       &= c_1 \cdot r + c_2 \cdot r^2 \\
                     &= r(c_1 + c_2 \cdot r) \\
\\
f(x)                 &= \text{Scale}(E) \times \text{Value}_1 \times (1 + \text{poly}(r))
\end{aligned}
\end{equation}
$$

### Algorithm Breakdown

- **Input Decomposition**: Split input $x$ into exponent $E$ and mantissa $M$ (or integer and fractional parts for EXP2)
- **Table Lookup**: Use mantissa high bits $M_{high}$ (7 bits) to index LUT for $\text{Value}_1$ and normalization factor $T$
- **Normalized Residual**: Compute $r = M_{low} \times T$, where $r \in [0, \frac{1}{2^{8}}]$
- **Polynomial Approximation**: Second-order Taylor approximation $\text{poly}(r) = r(c_1 + c_2 \cdot r)$
- **Result Composition**: $\text{Scale}(E)$ handles exponent part, $\text{Value}_1$ is table value, $\text{poly}(r)$ provides polynomial correction

## Hardware Design

### Pipeline Structure

```
S0: Input Filtering and Special Value Handling (FilterFP32)
    - Handle NaN, ±Inf, ±0, negative numbers (for specific functions)
    - Handle out-of-range inputs (EXP2 overflow/underflow, etc.)
    - Output bypass flag and bypass value
    - Pass normal inputs to next stage

S1: Input Decomposition (DecomposeFP32)
    - Floating-point functions: Extract exponent E and mantissa M = M_high + M_low
    - EXP2: Separate integer I = floor(x) and fractional F = x - I
    - M_high / F_high used as LUT index (7 bits)
    - M_low / F_low converted to FP32 format

S2: Lookup Table (LUT)
    - Index: M_high or F_high (7 bits, 128 entries)
    - Output Value1: Primary function value (log₂(1+M_high), √(1+M_high), etc.)
    - Output Value2: Normalization factor T = 1/(1+M_high)
    - SQRT/RSQRT select table entries based on E mod 2

S3: Normalized Residual Calculation (MUL0)
    - Input: M_low (or F_low) and T
    - Compute: r = M_low × T
    - Output: FP32 format normalized residual

S4: Polynomial First Stage (CMA0)
    - Input: r, c₁, c₂
    - Compute: t = c₂ × r + c₁
    - Uses FMA to reduce rounding errors

S5: Polynomial Second Stage (CMA1)
    - Input: r, t, c₀
    - Compute: poly = r × t + c₀
    - c₀ selected per function (EXP2: 1.0, LOG2: Value1, others: 1.0)

S6: Mantissa Composition / Integer Addition (Parallel)
    [Branch 1] Mantissa Composition (MUL1)
        - Input: Value1, poly
        - Compute: mant = Value1 × poly
        - Used for EXP2/RCP/SQRT/RSQRT

    [Branch 2] Integer Addition (INT8ADDFP32)
        - Input: E (SInt(8)), poly (FP32)
        - Compute: result = E + poly
        - Used for LOG2

S7: Exponent Adjustment and Result Assembly (RecomposeFP32)
    - Select input based on function type (mul1 or int8add result)
    - Adjust exponent:
      · EXP2: E_out = E_mant + I
      · LOG2: Direct output of int8add result
      · RCP: E_out = 254 - E_in + E_mant, preserve sign
      · SQRT: E_out = E_mant + ⌊E/2⌋
      · RSQRT: E_out = E_mant - ⌊E/2⌋
    - If bypass flag set, output bypassVal; otherwise output adjusted result
```

### Key Features

- **Unified Architecture**: Single pipeline serves all five functions
- **7-Stage Pipeline**: One result per cycle after initial latency
- **128-Entry LUTs**: Separate tables optimized for each function
- **Second-Order Approximation**: Balances accuracy and hardware complexity
- **Special Value Handling**: Comprehensive coverage of edge cases

## Accuracy Verification

Tested on 8,388,608 floating-point numbers in the interval $[1,2]$ to characterize approximation error bounds.

### 1. EXP2 (2^x)

**CPU Reference** (Standard C Library):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=3.632537e-08, MaxRelErr=2.358752e-07
AvgAbsErr=1.033031e-07, MaxAbsErr=4.768372e-07
AvgULP=0.43, MaxULP=2
```

**GPU Reference** (NVIDIA CUDA):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=4.985170e-08, MaxRelErr=3.106841e-07
AvgAbsErr=1.389425e-07, MaxAbsErr=7.152557e-07
AvgULP=0.58, MaxULP=3
```

### 2. LOG2 (log₂(x))

**CPU Reference** (Standard C Library):

```
Total=8388608, Pass=8369020 (99.77%), Fail=19588 (0.23%)
AvgRelErr=1.905063e-07, MaxRelErr=2.035354e-05
AvgAbsErr=2.722701e-08, MaxAbsErr=2.281740e-07
AvgULP=2.25, MaxULP=245
```

**GPU Reference** (NVIDIA CUDA):

```
Total=8388608, Pass=8334718 (99.36%), Fail=53890 (0.64%)
AvgRelErr=1.065914e-06, MaxRelErr=3.842898e-01
AvgAbsErr=6.248189e-08, MaxAbsErr=3.427267e-07
AvgULP=13.18, MaxULP=6114246
```

### 3. RCP (1/x)

**CPU Reference** (Standard C Library):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=5.899027e-08, MaxRelErr=5.448159e-07
AvgAbsErr=4.385801e-08, MaxAbsErr=5.364418e-07
AvgULP=0.74, MaxULP=9
```

**GPU Reference** (NVIDIA CUDA):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=6.097077e-08, MaxRelErr=5.448201e-07
AvgAbsErr=4.532859e-08, MaxAbsErr=5.364418e-07
AvgULP=0.76, MaxULP=9
```

### 4. SQRT (√x)

**CPU Reference** (Standard C Library):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=3.732800e-08, MaxRelErr=2.325453e-07
AvgAbsErr=4.531020e-08, MaxAbsErr=2.384186e-07
AvgULP=0.38, MaxULP=2
```

**GPU Reference** (NVIDIA CUDA):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=4.005722e-08, MaxRelErr=2.347089e-07
AvgAbsErr=4.860250e-08, MaxAbsErr=2.384186e-07
AvgULP=0.41, MaxULP=2
```

### 5. RSQRT (1/√x)

**CPU Reference** (Standard C Library):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=3.785930e-08, MaxRelErr=3.003264e-07
AvgAbsErr=3.186491e-08, MaxAbsErr=2.980232e-07
AvgULP=0.53, MaxULP=5
```

**GPU Reference** (NVIDIA CUDA):

```
Total=8388608, Pass=8388608 (100.00%), Fail=0 (0.00%)
AvgRelErr=3.582606e-08, MaxRelErr=2.030197e-07
AvgAbsErr=3.020077e-08, MaxAbsErr=1.788139e-07
AvgULP=0.51, MaxULP=3
```

### Error Metrics Summary

| Function | Avg ULP (CPU/GPU) | Max ULP (CPU/GPU) | Pass Rate |
|----------|-------------------|-------------------|-----------|
| EXP2     | 0.43 / 0.58       | 2 / 3             | 100.00%   |
| LOG2     | 2.25 / 13.18      | 245 / 6114246     | 99.77% / 99.36% |
| RCP      | 0.74 / 0.76       | 9 / 9             | 100.00%   |
| SQRT     | 0.38 / 0.41       | 2 / 2             | 100.00%   |
| RSQRT    | 0.53 / 0.51       | 5 / 3             | 100.00%   |

**Note**: LOG2 shows higher error rates and ULP values due to the challenging nature of logarithm approximation near 1.0, where relative errors are magnified.

## Dependencies

### Required

- **Chisel 6.6.0**: Hardware description language
- **Scala 2.13.15**: Programming language for Chisel
- **Mill**: Build tool for Scala/Chisel projects
- **Verilator**: For simulation and verification
- **XiangShan Fudian**: Floating-point arithmetic library (included as git submodule)

### Optional

- **CUDA/NVCC**: For GPU-accelerated reference implementation (NVIDIA GPU required)
- **Synopsys Design Compiler**: For ASIC synthesis

## Building

### Initialize Dependencies

```bash
make init
```

This will initialize the XiangShan Fudian submodule.

### Generate SystemVerilog

```bash
# Generate SFU RTL
./mill --no-server SFU.run
```

The generated SystemVerilog will be placed in `rtl/SFU.sv`.

### Build and Run Simulation

```bash
make run
```

The build system automatically detects CUDA availability:

- **Without CUDA**: Uses CPU reference only (standard C math library)
- **With CUDA**: Uses both CPU and GPU references simultaneously
  - CPU Reference: Standard C math library
  - GPU Reference: NVIDIA CUDA math library with `-use_fast_math` flag
  - Both error statistics are computed and displayed for comparison

### Clean Build Artifacts

```bash
make clean
```

## Testing and Verification

### Simulation

Verilator-based testbench with:

- Comprehensive test vector generation across full FP32 range
- Special value testing (NaN, Inf, zero, negative numbers, subnormals)
- ULP (Unit in Last Place) error measurement
- Waveform generation (FST format) for debugging

### Reference Models

The testbench automatically uses available reference implementations:

- **CPU Reference**: Standard C math library - always available
- **GPU Reference**: NVIDIA CUDA math library with `-use_fast_math` - automatically enabled if CUDA is detected

When both references are available, error statistics are computed against both to provide comprehensive verification.

### Accuracy Metrics

- **ULP Error**: Measures floating-point accuracy in terms of "units in the last place"
- **Relative Error**: Percentage deviation from reference value
- **Absolute Error**: Absolute difference from reference value
- **Pass/Fail**: Based on acceptable ULP threshold

## Future Improvements

- [ ] Optimize polynomial coefficients using Remez algorithm
- [ ] Add configurable rounding mode support
- [ ] Implement denormal number handling

## Credits

- **XiangShan Fudian FPU Library**: Provides high-quality floating-point arithmetic components
  - Repository: <https://github.com/OpenXiangShan/fudian>
  - Used for: FMUL, FCMA_ADD, RawFloat utilities

## References

- IEEE Standard for Floating-Point Arithmetic (IEEE 754-2008)
- XiangShan Fudian FPU: <https://github.com/OpenXiangShan/fudian>
- Chisel/FIRRTL Documentation: <https://www.chisel-lang.org/>
- CUDA Math API: <https://docs.nvidia.com/cuda/cuda-math-api/>
- Handbook of Floating-Point Arithmetic (Muller et al.)

## License

This project reuses the XiangShan Fudian library. Please refer to the respective license files in the `dependencies/fudian` directory for licensing terms.
