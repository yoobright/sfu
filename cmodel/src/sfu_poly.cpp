#include "sfu_poly.h"
#include <stdexcept>

uint32_t SFUPoly::sigmoid_lut_entries = 128;

void SFUPoly::configure_sigmoid(uint32_t lut_entries) {
  if (lut_entries != 64 && lut_entries != 128) {
    throw std::invalid_argument("sigmoid LUT must contain 64 or 128 entries");
  }
  sigmoid_lut_entries = lut_entries;
}

PolyOutput SFUPoly::compute(const LUTOutput &input, SFUOp op) {
  PolyOutput out;
  out.exp = input.exp;
  out.sign = input.sign;

  const FunctionParams &params = Function::get(op);

  uint64_t xl = input.xl;
  uint64_t xl2_full = xl * xl;

  int shift0 = params.shift0();
  uint32_t xl2 = (xl2_full >> shift0) & 0x7FFF;

  int64_t c1_xl = (int64_t)input.c1 * (int64_t)xl;
  int64_t c2_xl2 = (int64_t)input.c2 * (int64_t)xl2;

  int shift1 = params.shift1();
  int shift2 = params.shift2();
  if (op == SFUOp::SIGMOID && sigmoid_lut_entries == 64) {
    // The nonuniform 64-entry layout needs four times the C2 coefficient
    // range used by the default 128-entry layout.
    shift2 -= 2;
  }

  int64_t aligned1 = c1_xl >> shift1;
  int64_t aligned2 = c2_xl2 >> shift2;

  int64_t sum = (int64_t)input.c0 + aligned1 + aligned2;

  out.result = sum & 0x7FFFFFF;

  return out;
}
