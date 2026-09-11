#include "sfu_compose.h"
#include <cstdint>
#include <cstdio>

static int count_leading_zeros(uint64_t x) {
  if (x == 0)
    return 64;
  int count = 0;
  uint64_t mask = 1ULL << 63;
  while ((x & mask) == 0) {
    count++;
    mask >>= 1;
  }
  return count;
}

// Convert an unsigned Q0.26 magnitude to FP32 without adding another
// approximation stage. The polynomial table is generated for this exact
// truncating normalization behavior.
static uint32_t q26_to_float(uint32_t fixed, uint8_t sign = 0) {
  if (fixed == 0)
    return static_cast<uint32_t>(sign) << 31;
  int leading_bit = 31 - __builtin_clz(fixed);
  uint8_t exp_out = static_cast<uint8_t>(leading_bit + 101);
  uint32_t normalized = leading_bit > 23 ? fixed >> (leading_bit - 23)
                                         : fixed << (23 - leading_bit);
  return (static_cast<uint32_t>(sign) << 31) |
         (static_cast<uint32_t>(exp_out) << 23) | (normalized & 0x7FFFFF);
}

uint32_t SFUCompose::compose(const PolyOutput &input, SFUOp op) {
  uint32_t result = 0;
  uint32_t poly_result = input.result;
  int8_t exp = input.exp;
  uint8_t sign = input.sign;

  switch (op) {
  case SFUOp::EXP2:
  case SFUOp::EXP: {
    uint8_t exp_out = (127 + exp) & 0xFF;
    uint32_t mant_out = (poly_result >> 2) & 0x7FFFFF;
    result = (exp_out << 23) | mant_out;
    break;
  }

  case SFUOp::LOG2: {
    bool sign = (exp < 0);
    uint64_t sum = ((uint64_t)(exp & 0xFF) << 26) | poly_result;
    uint64_t sum_abs = sign ? ((~sum + 1) & 0x3FFFFFFFF) : sum;

    int lzd = count_leading_zeros(sum_abs << 30);
    uint8_t exp_out = (134 - lzd) & 0xFF;
    uint32_t mant_out = ((sum_abs << lzd) >> 10) & 0x7FFFFF;

    result = (sign << 31) | (exp_out << 23) | mant_out;
    break;
  }

  case SFUOp::RCP: {
    uint8_t exp_out = (126 - exp) & 0xFF;
    uint32_t mant_out = (poly_result >> 2) & 0x7FFFFF;
    result = sign << 31 | (exp_out << 23) | mant_out;
    break;
  }

  case SFUOp::SQRT: {
    uint8_t exp_out = (127 + exp) & 0xFF;
    uint32_t mant_out = (poly_result >> 2) & 0x7FFFFF;
    result = (exp_out << 23) | mant_out;
    break;
  }

  case SFUOp::RSQRT: {
    uint8_t exp_out = (126 - exp) & 0xFF;
    uint32_t mant_out = (poly_result >> 2) & 0x7FFFFF;
    result = (exp_out << 23) | mant_out;
    break;
  }

  case SFUOp::SIN:
  case SFUOp::COS: {
    if ((poly_result >> 26) & 0x1) {
      result = sign << 31 | (0x7F << 23);
    } else {
      int lzd = count_leading_zeros((uint64_t)poly_result << 38);
      uint8_t exp_out = (126 - lzd) & 0xFF;
      uint32_t mant_out = ((poly_result << lzd) >> 2) & 0x7FFFFF;

      result = (sign << 31) | (exp_out << 23) | mant_out;
    }
    break;
  }
  case SFUOp::SIGMOID: {
    uint32_t half_tanh = poly_result >> 1;
    uint32_t sigmoid_fixed =
        sign ? ((1U << 25) - half_tanh) : ((1U << 25) + half_tanh);
    result = q26_to_float(sigmoid_fixed);
    break;
  }
  case SFUOp::TANH: {
    result = q26_to_float(poly_result, sign);
    break;
  }
  }

  return result;
}
