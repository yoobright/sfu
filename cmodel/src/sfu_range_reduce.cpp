#include "sfu_range_reduce.h"

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
  uint8_t quadrand = int_part & 0x3;

  out.sign = input.sign;

  switch (op) {
  case SFUOp::EXP2:
    out.exp = (input.sign) ? -int_part_floor : int_part_floor;
    out.index = (frac_part_floor >> 17) & 0x3F;
    out.xl = frac_part_floor & 0x1FFFF;
    break;

  case SFUOp::EXP: {
    // Multiply the full 24-bit significand before shifting. This avoids
    // truncating small inputs to Q23 before the log2(e) multiplication.
    constexpr uint64_t LOG2_E_Q31 = 3098164009ULL;
    uint64_t product = static_cast<uint64_t>(sig) * LOG2_E_Q31;
    int shift = 31 - exp_signed; // live inputs have exponent <= 6
    uint64_t scaled = shift >= 64 ? 0 :
        (product + (1ULL << (shift - 1))) >> shift;
    int integer = static_cast<int>(scaled >> 23);
    uint32_t fraction = scaled & 0x7FFFFF;
    if (input.sign) {
      integer = -integer;
      if (fraction != 0) {
        --integer;
        fraction = (1U << 23) - fraction;
      }
    }
    out.exp = integer;
    out.index = fraction >> 17;
    out.xl = fraction & 0x1FFFF;
    break;
  }

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
  case SFUOp::TANH: {
    out.exp = 0;
    // Both operations address tanh(|x|). SIGMOID first scales by 1/2 and
    // composes (1 +/- tanh(|x|/2))/2 after polynomial evaluation.
    uint64_t tanh_arg = op == SFUOp::SIGMOID ? sig_shifted >> 1 : sig_shifted;
    if (tanh_arg < (1U << 23)) {
      // [0, 1): 64 intervals of width 1/64.
      out.index = (tanh_arg >> 17) & 0x3F;
      out.xl = (tanh_arg >> 1) & 0xFFFF;
    } else if (tanh_arg < (4U << 23)) {
      // [1, 4): 48 intervals of width 1/16.
      uint64_t delta = tanh_arg - (1U << 23);
      out.index = 64 + (delta >> 19);
      out.xl = (delta >> 3) & 0xFFFF;
    } else {
      // [4, 8): 16 intervals of width 1/4.
      uint64_t delta = tanh_arg - (4U << 23);
      out.index = 112 + (delta >> 21);
      out.xl = (delta >> 5) & 0xFFFF;
    }
    break;
  }
  default:
    break;
  }

  if (op == SFUOp::EXP || op == SFUOp::EXP2) {
    // Keep the 17-bit magnitude unsigned. The exact left endpoint is 65536,
    // so it must not be narrowed to 16 bits. Reuse sign as a direction flag.
    constexpr uint32_t center = 1U << 16;
    uint32_t local = out.xl;
    out.sign = local < center;
    out.xl = out.sign ? center - local : local - center;
  }
  return out;
}
