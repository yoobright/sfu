#include "sfu_poly.h"

PolyOutput SFUPoly::compute(const LUTOutput &input, SFUOp op) {
  PolyOutput out;
  out.exp = input.exp;
  out.sign = input.sign;

  const FunctionParams &params = Function::get(op);

  uint64_t xl = input.xl;
  uint64_t xl2_full = xl * xl;

  int shift0 = params.shift0();
  uint32_t xl2 = (xl2_full >> shift0) & 0x7FFF;

  if (params.centered) {
    // Positive coefficients and magnitudes only: no signed multiply/square.
    // Bias is 1/2 below segment 34, otherwise 1. The center's c0 selects
    // it without another pipeline flag. c1 stores (slope - bias) in Q17.
    unsigned bias_shift = input.c0 < 48408813 ? 16 : 17;
    uint64_t linear_product = static_cast<uint32_t>(input.c1) * xl + (xl << bias_shift);
    uint64_t quadratic_product = static_cast<uint64_t>(input.c2) * xl2;
    uint32_t linear = linear_product >> params.shift1();
    if (input.sign && (linear_product & ((1ULL << params.shift1()) - 1)))
      ++linear; // floor(-v/32768) == -ceil(v/32768)
    uint32_t base = static_cast<uint32_t>(input.c0) +
                    (quadratic_product >> params.shift2());
    out.result = input.sign ? base - linear : base + linear;
    return out;
  }

  int64_t c1_xl = (int64_t)input.c1 * (int64_t)xl;
  int64_t c2_xl2 = (int64_t)input.c2 * (int64_t)xl2;

  int shift1 = params.shift1();
  int shift2 = params.shift2();

  int64_t aligned1 = c1_xl >> shift1;
  int64_t aligned2 = c2_xl2 >> shift2;

  int64_t sum = (int64_t)input.c0 + aligned1 + aligned2;

  out.result = sum & 0x7FFFFFF;

  return out;
}
