#include "sfu_core.h"
#include "sfu_filter.h"
#include "sfu_range_reduce.h"
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

bool test_configuration(uint32_t lut_entries) {
  SFUCore::init(lut_entries);
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

  double max_abs_error = 0.0;
  double max_monotonic_reversal = 0.0;
  float previous = 0.0f;
  for (uint32_t segment = 0; segment < lut_entries; ++segment) {
    float start;
    float width;
    if (lut_entries == 64 && segment < 32) {
      start = static_cast<float>(segment) / 16.0f;
      width = 1.0f / 16.0f;
    } else if (lut_entries == 64 && segment < 52) {
      start = 2.0f + static_cast<float>(segment - 32) / 10.0f;
      width = 1.0f / 10.0f;
    } else if (lut_entries == 64) {
      start = 4.0f + static_cast<float>(segment - 52) / 6.0f;
      width = 1.0f / 6.0f;
    } else if (segment < 64) {
      start = static_cast<float>(segment) / 32.0f;
      width = 1.0f / 32.0f;
    } else {
      start = 2.0f + static_cast<float>(segment - 64) / 16.0f;
      width = 1.0f / 16.0f;
    }
    for (uint32_t local = 0; local < (1U << 16); ++local) {
      float input = start + width * static_cast<float>(local) * 0x1p-16f;
      if (local == (1U << 15)) {
        FilterOutput filtered =
            SFUFilter::filter(bits(input), SFUOp::SIGMOID);
        RangeReduceOutput reduced =
            SFURangeReduce::reduce(filtered, SFUOp::SIGMOID);
        if (reduced.index != segment) {
          std::cerr << "segment " << segment << " is not reachable\n";
          return false;
        }
      }
      float actual = SFUCore::compute(input, SFUOp::SIGMOID);
      float reference = sigmoid_reference(input);
      max_abs_error = std::max(
          max_abs_error, std::abs(static_cast<double>(actual) - reference));

      if ((segment != 0 || local != 0) && actual < previous) {
        max_monotonic_reversal =
            std::max(max_monotonic_reversal,
                     static_cast<double>(previous) - actual);
      }
      previous = actual;

      if ((local & 0xFFF) == 0) {
        float negative = SFUCore::compute(-input, SFUOp::SIGMOID);
        if (std::abs((static_cast<double>(actual) + negative) - 1.0) >
            1.2e-7) {
          std::cerr << "symmetry failed at input=" << input << '\n';
          return false;
        }
      }
    }
  }

  std::cout << "sigmoid " << lut_entries
            << "-entry max absolute error: " << max_abs_error << '\n';
  std::cout << "sigmoid " << lut_entries
            << "-entry max segment-boundary reversal: "
            << max_monotonic_reversal << '\n';
  double accuracy_limit = lut_entries == 128 ? 3.1e-7 : 5.5e-7;
  if (max_abs_error > accuracy_limit) {
    std::cerr << "accuracy limit exceeded\n";
    ok = false;
  }
  // The Q0.26-to-FP32 truncation may introduce at most one FP32 ULP at 0.5.
  if (max_monotonic_reversal > 6.0e-8) {
    std::cerr << "monotonicity tolerance exceeded\n";
    ok = false;
  }

  return ok;
}

int main() {
  bool ok = test_configuration(64);
  ok &= test_configuration(128);
  return ok ? 0 : 1;
}
