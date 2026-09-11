#include "sfu_range_reduce.h"
#include <cstdio>
#include <stdexcept>

uint32_t SFURangeReduce::sigmoid_lut_entries = 128;

void SFURangeReduce::configure_sigmoid(uint32_t lut_entries) {
  if (lut_entries != 64 && lut_entries != 128) {
    throw std::invalid_argument("sigmoid LUT must contain 64 or 128 entries");
  }
  sigmoid_lut_entries = lut_entries;
}

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

  // log2(e), Q1.31. EXP first maps e^x to 2^(x * log2(e)); the rounded
  // product remains Q?.23 so it can use the existing EXP2 split and LUT.
  constexpr uint64_t LOG2_E_Q31 = 3098164009ULL;
  uint64_t exp_scaled =
      (sig_shifted * LOG2_E_Q31 + (1ULL << 30)) >> 31;
  uint32_t exp_int_part = (exp_scaled >> 23) & 0xFF;
  uint32_t exp_frac_part = exp_scaled & 0x7FFFFF;
  uint32_t exp_frac_part_floor =
      exp_frac_part == 0 ? 0 : ((~exp_frac_part + 1) & 0x7FFFFF);
  uint8_t exp_int_part_floor =
      exp_frac_part == 0 ? exp_int_part : exp_int_part + 1;

  out.sign = input.sign;

  switch (op) {
  case SFUOp::EXP2:
    out.exp = (input.sign) ? -int_part_floor : int_part_floor;
    out.index = (frac_part_floor >> 17) & 0x3F;
    out.xl = frac_part_floor & 0x1FFFF;
    break;

  case SFUOp::EXP:
    out.exp = -static_cast<int8_t>(exp_int_part_floor);
    out.index = (exp_frac_part_floor >> 17) & 0x3F;
    out.xl = exp_frac_part_floor & 0x1FFFF;
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
    // Select a region and split the local coordinate into a table index and a
    // 16-bit fraction.  The default 128-entry layout uses power-of-two bit
    // slices; the 64-entry experiment uses shift-add scaling.  All entries
    // are reachable before the |x| >= 6 saturation boundary.
    if (sigmoid_lut_entries == 128) {
      if (int_part < 2) {
        // [0, 2): 64 intervals of width 1/32.
        out.index = (sig_shifted >> 18) & 0x3F;
        out.xl = (sig_shifted >> 2) & 0xFFFF;
      } else {
        // [2, 6): 64 intervals of width 1/16.
        out.index = 64 + (((sig_shifted >> 19) & 0x7F) - 32);
        out.xl = (sig_shifted >> 3) & 0xFFFF;
      }
    } else {
      // The 64-entry experiment gives [2, 4) more entries, where the
      // quadratic approximation previously reached its worst error.
      uint64_t scaled;
      if (int_part < 2) {
        // [0, 2): 32 intervals of width 1/16.
        out.index = (sig_shifted >> 19) & 0x1F;
        out.xl = (sig_shifted >> 3) & 0xFFFF;
      } else if (int_part < 4) {
        // [2, 4): 20 intervals of width 1/10.
        uint64_t delta = sig_shifted - (2U << 23);
        scaled = (delta << 3) + (delta << 1);
        out.index = 32 + ((scaled >> 23) & 0x1F);
        out.xl = (scaled >> 7) & 0xFFFF;
      } else {
        // [4, 6): 12 intervals of width 1/6.
        uint64_t delta = sig_shifted - (4U << 23);
        scaled = (delta << 2) + (delta << 1);
        out.index = 52 + ((scaled >> 23) & 0xF);
        out.xl = (scaled >> 7) & 0xFFFF;
      }
    }
    break;
  default:
    break;
  }

  return out;
}
