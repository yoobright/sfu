#include "sfu_core.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

float sigmoid_reference(float x) {
  if (x >= 0.0f)
    return static_cast<float>(1.0 / (1.0 + std::exp(-static_cast<double>(x))));
  double exp_x = std::exp(static_cast<double>(x));
  return static_cast<float>(exp_x / (1.0 + exp_x));
}

uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

bool check_special(float input, uint32_t expected) {
  float actual = SFUCore::compute(input, SFUOp::SIGMOID);
  if (bits(actual) == expected)
    return true;
  std::cerr << "special case failed: input=" << input << " actual=0x"
            << std::hex << bits(actual) << " expected=0x" << expected
            << std::dec << '\n';
  return false;
}

} // namespace

int main() {
  SFUCore::init();

  bool ok = true;
  ok &= check_special(0.0f, 0x3F000000);
  ok &= check_special(-0.0f, 0x3F000000);
  ok &= check_special(std::numeric_limits<float>::infinity(), 0x3F800000);
  ok &= check_special(-std::numeric_limits<float>::infinity(), 0x00000000);
  ok &= check_special(6.0f, 0x3F800000);
  ok &= check_special(-6.0f, 0x00000000);

  float below_saturation = std::nextafter(6.0f, 0.0f);
  if (bits(SFUCore::compute(below_saturation, SFUOp::SIGMOID)) ==
          0x3F800000 ||
      bits(SFUCore::compute(-below_saturation, SFUOp::SIGMOID)) ==
          0x00000000) {
    std::cerr << "sigmoid saturated below |x| = 6\n";
    ok = false;
  }

  float nan_result = SFUCore::compute(std::numeric_limits<float>::quiet_NaN(),
                                      SFUOp::SIGMOID);
  if (!std::isnan(nan_result)) {
    std::cerr << "NaN input did not produce NaN\n";
    ok = false;
  }

  // |x| / 8 uses Q0.23, so [0, 6) contains 6 * 2^20 values.
  constexpr uint32_t kReducedValues = 6U << 20;
  double max_abs_error = 0.0;
  double max_monotonic_reversal = 0.0;
  float previous = 0.0f;
  for (uint32_t reduced = 0; reduced < kReducedValues; ++reduced) {
    float input = static_cast<float>(reduced) * 0x1p-20f;
    float actual = SFUCore::compute(input, SFUOp::SIGMOID);
    float reference = sigmoid_reference(input);
    max_abs_error =
        std::max(max_abs_error, std::abs(static_cast<double>(actual) - reference));

    if (reduced != 0 && actual < previous) {
      max_monotonic_reversal =
          std::max(max_monotonic_reversal,
                   static_cast<double>(previous) - actual);
    }
    previous = actual;

    if ((reduced & 0xFFF) == 0) {
      float negative = SFUCore::compute(-input, SFUOp::SIGMOID);
      if (std::abs((static_cast<double>(actual) + negative) - 1.0) >
          1.2e-7) {
        std::cerr << "symmetry failed at input=" << input << '\n';
        ok = false;
        break;
      }
    }
  }

  std::cout << "sigmoid max absolute error: " << max_abs_error << '\n';
  std::cout << "sigmoid max segment-boundary reversal: "
            << max_monotonic_reversal << '\n';
  if (max_abs_error > 6.1e-7) {
    std::cerr << "accuracy limit exceeded\n";
    ok = false;
  }
  // The Q0.26-to-FP32 truncation may introduce at most one FP32 ULP at 0.5.
  if (max_monotonic_reversal > 6.0e-8) {
    std::cerr << "monotonicity tolerance exceeded\n";
    ok = false;
  }

  return ok ? 0 : 1;
}
