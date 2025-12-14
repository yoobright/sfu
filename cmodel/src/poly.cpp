#include "sfu_poly.h"

PolyOutput SFUPoly::compute(const LUTOutput &input, SFUOp op) {
  PolyOutput out;
  out.exp = input.exp;

  const FunctionParams &params = Function::get(op);

  int xl_width = 23 - params.m;
  uint64_t xl = input.xl;
  uint64_t xl2_full = xl * xl;

  int squarer_truncate_bits = xl_width * 2 - SFUConfig::squarer_output_width;
  uint32_t xl2 = (xl2_full >> squarer_truncate_bits) & 0x7FFF;

  int64_t c1_xl = (int64_t)input.c1 * (int64_t)xl;
  int64_t c2_xl2 = (int64_t)input.c2 * (int64_t)xl2;

  int shift1 = params.shift1();
  int shift2 = params.shift2();

  int64_t aligned1 = c1_xl >> shift1;
  int64_t aligned2 = c2_xl2 >> shift2;

  int64_t sum = (int64_t)input.c0 + aligned1 + aligned2;

  out.result = sum & 0x3FFFFFF;

  return out;
}
