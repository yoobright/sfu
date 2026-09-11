#include "sfu_core.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

uint32_t ulp_distance(float a, float b) {
  uint32_t a_bits = bits(a);
  uint32_t b_bits = bits(b);
  return a_bits > b_bits ? a_bits - b_bits : b_bits - a_bits;
}

bool expect_bits(float input, uint32_t expected) {
  float actual = SFUCore::compute(input, SFUOp::EXP);
  if (bits(actual) == expected)
    return true;
  std::cerr << "EXP special case failed: input=" << input << " actual=0x"
            << std::hex << bits(actual) << " expected=0x" << expected
            << std::dec << '\n';
  return false;
}

} // namespace

int main() {
  SFUCore::init();
  bool ok = true;

  ok &= expect_bits(0.0f, 0x3F800000);
  ok &= expect_bits(-0.0f, 0x3F800000);
  ok &= expect_bits(1.0f, 0x3F800000);
  ok &= expect_bits(std::numeric_limits<float>::infinity(), 0x3F800000);
  ok &= expect_bits(-17.0f, 0x00000000);
  ok &= expect_bits(-std::numeric_limits<float>::infinity(), 0x00000000);

  float nan = SFUCore::compute(std::numeric_limits<float>::quiet_NaN(),
                               SFUOp::EXP);
  if (!std::isnan(nan)) {
    std::cerr << "EXP NaN input did not produce NaN\n";
    ok = false;
  }

  float at_min = SFUCore::compute(-16.0f, SFUOp::EXP);
  float min_reference = static_cast<float>(std::exp(-16.0));
  if (at_min == 0.0f || ulp_distance(at_min, min_reference) > 2) {
    std::cerr << "EXP -16 boundary was saturated or inaccurate\n";
    ok = false;
  }
  ok &= expect_bits(std::nextafter(-16.0f,
                                   -std::numeric_limits<float>::infinity()),
                    0x00000000);

  uint32_t max_ulp = 0;
  float previous = 0.0f;
  constexpr uint32_t kSamples = 1U << 22;
  for (uint32_t i = 0; i <= kSamples; ++i) {
    float input = -16.0f + 16.0f * static_cast<float>(i) / kSamples;
    float actual = SFUCore::compute(input, SFUOp::EXP);
    float reference = static_cast<float>(std::exp(static_cast<double>(input)));
    max_ulp = std::max(max_ulp, ulp_distance(actual, reference));
    if (i != 0 && actual < previous) {
      std::cerr << "EXP monotonicity failure at x=" << input << '\n';
      ok = false;
      break;
    }
    previous = actual;
  }

  std::cout << "EXP [-16, 0] sampled max ULP: " << max_ulp << '\n';
  if (max_ulp > 2) {
    std::cerr << "EXP accuracy limit exceeded\n";
    ok = false;
  }

  return ok ? 0 : 1;
}
