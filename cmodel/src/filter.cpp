#include "sfu_filter.h"
#include <cmath>

FilterOutput SFUFilter::filter(uint32_t input_bits, SFUOp op) {
  FilterOutput out;
  out.bypass = false;
  out.bypass_val = 0;

  uint8_t sign = (input_bits >> 31) & 1;
  uint8_t exp = (input_bits >> 23) & 0xFF;
  uint32_t mant = input_bits & 0x7FFFFF;

  bool is_nan = (exp == 0xFF) && (mant != 0);
  bool is_inf = (exp == 0xFF) && (mant == 0);
  bool is_zero = (exp == 0) && (mant == 0);

  if (is_nan) {
    out.bypass = true;
    out.bypass_val = 0x7FFFFFFF;
    return out;
  }

  switch (op) {
  case SFUOp::EXP2:
    if (is_inf) {
      out.bypass = true;
      out.bypass_val = sign ? 0x00000000 : 0x7F800000;
    } else if (is_zero) {
      out.bypass = true;
      out.bypass_val = 0x3F800000;
    } else {
      int8_t exp_signed = exp - 127;
      if (sign && exp_signed > 7) {
        out.bypass = true;
        out.bypass_val = 0x00000000;
      } else if (!sign && exp_signed > 7) {
        out.bypass = true;
        out.bypass_val = 0x7F800000;
      }
    }
    break;

  case SFUOp::LOG2:
    if (sign && !is_zero) {
      out.bypass = true;
      out.bypass_val = 0x7FFFFFFF;
    } else if (is_zero) {
      out.bypass = true;
      out.bypass_val = 0xFF800000;
    } else if (is_inf) {
      out.bypass = true;
      out.bypass_val = 0x7F800000;
    }
    break;

  case SFUOp::RCP:
    if (is_zero) {
      out.bypass = true;
      out.bypass_val = sign ? 0xFF800000 : 0x7F800000;
    } else if (is_inf) {
      out.bypass = true;
      out.bypass_val = sign ? 0x80000000 : 0x00000000;
    }
    break;

  case SFUOp::SQRT:
    if (sign && !is_zero) {
      out.bypass = true;
      out.bypass_val = 0x7FFFFFFF;
    } else if (is_zero || is_inf) {
      out.bypass = true;
      out.bypass_val = input_bits;
    }
    break;

  case SFUOp::RSQRT:
    if (sign && !is_zero) {
      out.bypass = true;
      out.bypass_val = 0x7FFFFFFF;
    } else if (is_zero) {
      out.bypass = true;
      out.bypass_val = 0x7F800000;
    } else if (is_inf) {
      out.bypass = true;
      out.bypass_val = 0x00000000;
    }
    break;
  }

  out.sign = sign;
  out.exponent = exp;
  out.mantissa = mant;

  return out;
}
