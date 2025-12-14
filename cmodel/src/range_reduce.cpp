#include "sfu_range_reduce.h"

RangeReduceOutput SFURangeReduce::reduce(const FilterOutput &input, SFUOp op) {
  RangeReduceOutput out;

  uint32_t mantissa = input.mantissa;
  int8_t exp_signed = input.exponent - 127;
  uint32_t sig = (1 << 23) | mantissa;

  if (op == SFUOp::EXP2) {
    uint64_t sig_extended = (uint64_t)sig << 7;
    uint64_t sig_shifted;

    if (exp_signed >= 0) {
      sig_shifted = sig_extended << exp_signed;
    } else {
      sig_shifted = sig_extended >> (-exp_signed);
    }

    uint32_t int_part = (sig_shifted >> 30) & 0xFF;
    uint32_t frac_part = (sig_shifted >> 7) & 0x7FFFFF;
    bool is_frac_zero = (frac_part == 0);

    uint32_t int_part_floor, frac_part_floor;
    if (input.sign && !is_frac_zero) {
      int_part_floor = int_part + 1;
      frac_part_floor = (1 << 23) - frac_part;
    } else {
      int_part_floor = int_part;
      frac_part_floor = frac_part;
    }

    out.exp = input.sign ? -(int8_t)int_part_floor : (int8_t)int_part_floor;
    out.index = (frac_part_floor >> 17) & 0x3F;
    out.xl = frac_part_floor & 0x1FFFF;
  } else {
    out.exp = exp_signed;

    switch (op) {
    case SFUOp::LOG2:
      out.index = (mantissa >> 17) & 0x3F;
      out.xl = mantissa & 0x1FFFF;
      break;

    case SFUOp::RCP:
      out.index = (mantissa >> 16) & 0x7F;
      out.xl = mantissa & 0xFFFF;
      break;

    case SFUOp::SQRT:
    case SFUOp::RSQRT:
      out.index = ((exp_signed & 1) << 6) | ((mantissa >> 17) & 0x3F);
      out.xl = mantissa & 0x1FFFF;
      break;

    default:
      break;
    }
  }

  return out;
}
