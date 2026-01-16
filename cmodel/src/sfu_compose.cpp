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

uint32_t SFUCompose::compose(const PolyOutput &input, SFUOp op) {
  uint32_t result = 0;
  uint32_t poly_result = input.result;
  int8_t exp = input.exp;
  uint8_t sign = input.sign;

  switch (op) {
  case SFUOp::EXP2: {
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
  }

  return result;
}
