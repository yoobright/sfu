#ifndef SFU_TYPES_H
#define SFU_TYPES_H

#include <cstdint>
#include <cstdio>

enum class SFUOp {
  EXP2 = 0,
  LOG2 = 1,
  RCP = 2,
  SQRT = 3,
  RSQRT = 4,
  SIN = 5,
  COS = 6,
  SIGMOID = 7,
  EXP = 8
};

struct SFUConfig {
  static constexpr int c0_width = 26;
  static constexpr int c1_width = 16;
  static constexpr int c2_width = 12;
  static constexpr int squarer_output_width = 15;
};

struct FunctionParams {
  int m;
  int c0_sign, c1_sign, c2_sign;
  int c0_exp, c1_exp, c2_exp;

  int shift0() const { return (23 - m) * 2 - SFUConfig::squarer_output_width; }

  int shift1() const {
    int c0_real_exp = c0_exp - SFUConfig::c0_width + 1;
    int c1_real_exp = c1_exp - SFUConfig::c1_width + 1;
    int c1_xl_real_exp = c1_real_exp - 23;
    return c0_real_exp - c1_xl_real_exp;
  }

  int shift2() const {
    int c0_real_exp = c0_exp - SFUConfig::c0_width + 1;
    int c2_real_exp = c2_exp - SFUConfig::c2_width + 1;
    int c2_xl2_real_exp = c2_real_exp - 2 * m - SFUConfig::squarer_output_width;
    return c0_real_exp - c2_xl2_real_exp;
  }
};

class Function {
public:
  static const FunctionParams EXP2;
  static const FunctionParams LOG2;
  static const FunctionParams RCP;
  static const FunctionParams SQRT;
  static const FunctionParams RSQRT;
  static const FunctionParams SIN;
  static const FunctionParams SIGMOID;
  static const FunctionParams EXP;

  static const FunctionParams &get(SFUOp op);
};

struct FilterOutput {
  bool bypass;
  uint32_t bypass_val;
  uint8_t sign;
  uint8_t exponent;
  uint32_t mantissa;
};

struct RangeReduceOutput {
  uint8_t index;
  uint32_t xl;
  uint8_t sign;
  int8_t exp;
};

struct LUTOutput {
  int32_t c0;
  int32_t c1;
  int32_t c2;
  uint32_t xl;
  uint8_t sign;
  int8_t exp;
};

struct PolyOutput {
  uint32_t result;
  uint8_t sign;
  int8_t exp;
};

#endif
