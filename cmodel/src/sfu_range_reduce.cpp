#include "sfu_range_reduce.h"
#include <cstdio>

RangeReduceOutput SFURangeReduce::reduce(const FilterOutput &input, SFUOp op) {
  RangeReduceOutput out;

  uint32_t mantissa = input.mantissa;
  int8_t exp_signed = input.exponent - 127;
  uint32_t sig = (1 << 23) | mantissa;

  uint64_t sig_extended = sig;
  uint64_t sig_shifted;

  if (exp_signed >= 30) {
    sig_shifted = 1 << 30; // c does not handle over shifts
  } else if (exp_signed >= 0) {
    sig_shifted = sig_extended << exp_signed;
  } else if (exp_signed >= -23) {
    sig_shifted = sig_extended >> (-exp_signed);
  } else {
    sig_shifted = 0;
  }

  uint32_t int_part = (sig_shifted >> 23) & 0xFF;
  uint32_t frac_part = sig_shifted & 0x7FFFFF;
  uint32_t frac_part_inv = (~frac_part) & 0x7FFFFF;
  uint8_t int_part_floor =
      (input.sign && frac_part != 0) ? (int_part + 1) : int_part;
  uint32_t frac_part_floor = (input.sign && frac_part != 0)
                                 ? ((frac_part_inv + 1) & 0x7FFFFF)
                                 : frac_part;
  // sigmoid_arg is |x| / 8 represented as a 23-bit fraction. Inputs with
  // |x| >= 6 have already been saturated by the filter stage.
  uint32_t sigmoid_arg = (sig_shifted >> 3) & 0x7FFFFF;

  uint8_t quadrand = int_part & 0x3;

  out.sign = input.sign;

  switch (op) {
  case SFUOp::EXP2:
    out.exp = (input.sign) ? -int_part_floor : int_part_floor;
    out.index = (frac_part_floor >> 17) & 0x3F;
    out.xl = frac_part_floor & 0x1FFFF;
    break;

  case SFUOp::LOG2:
    out.exp = exp_signed;
    out.index = (mantissa >> 17) & 0x3F;
    out.xl = mantissa & 0x1FFFF;
    break;

  case SFUOp::RCP:
    out.exp = exp_signed;
    out.index = (mantissa >> 16) & 0x7F;
    out.xl = mantissa & 0xFFFF;
    break;

  case SFUOp::SQRT:
  case SFUOp::RSQRT:
    out.exp = exp_signed >> 1;
    out.index = ((exp_signed & 1) << 6) | ((mantissa >> 17) & 0x3F);
    out.xl = mantissa & 0x1FFFF;
    break;

  case SFUOp::SIN:
    out.exp = 0;
    switch (quadrand) {
    case 0:
      out.sign = input.sign;
      out.index = (frac_part >> 17) & 0x3F;
      out.xl = frac_part & 0x1FFFF;
      break;
    case 1:
      out.sign = input.sign;
      out.index = (frac_part_inv >> 17) & 0x3F;
      out.xl = frac_part_inv & 0x1FFFF;
      break;
    case 2:
      out.sign = !input.sign;
      out.index = (frac_part >> 17) & 0x3F;
      out.xl = frac_part & 0x1FFFF;
      break;
    case 3:
      out.sign = !input.sign;
      out.index = (frac_part_inv >> 17) & 0x3F;
      out.xl = frac_part_inv & 0x1FFFF;
      break;
    }
    break;

  case SFUOp::COS:
    out.exp = 0;
    switch (quadrand) {
    case 0:
      out.sign = 0;
      out.index = (frac_part_inv >> 17) & 0x3F;
      out.xl = frac_part_inv & 0x1FFFF;
      break;
    case 1:
      out.sign = 1;
      out.index = (frac_part >> 17) & 0x3F;
      out.xl = frac_part & 0x1FFFF;
      break;
    case 2:
      out.sign = 1;
      out.index = (frac_part_inv >> 17) & 0x3F;
      out.xl = frac_part_inv & 0x1FFFF;
      break;
    case 3:
      out.sign = 0;
      out.index = (frac_part >> 17) & 0x3F;
      out.xl = frac_part & 0x1FFFF;
      break;
    }
    break;
  case SFUOp::SIGMOID:
    out.exp = 0;
    out.index = (sigmoid_arg >> 16) & 0x7F;
    out.xl = sigmoid_arg & 0xFFFF;
    break;
  default:
    break;
  }

  return out;
}
