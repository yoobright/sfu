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
uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

bool expect_bits(float input, uint32_t expected) {
  float actual = SFUCore::compute(input, SFUOp::SIGMOID);
  if (bits(actual) == expected)
    return true;
  std::cerr << "SIGMOID special case failed: input=" << input << " actual=0x"
            << std::hex << bits(actual) << " expected=0x" << expected
            << std::dec << '\n';
  return false;
}
} // namespace

int main() {
  SFUCore::init();
  bool ok = true;
  ok &= expect_bits(0.0f, 0x3F000000);
  ok &= expect_bits(-0.0f, 0x3F000000);
  ok &= expect_bits(8.0f, 0x3F800000);
  ok &= expect_bits(-8.0f, 0x00000000);
  ok &= expect_bits(std::numeric_limits<float>::infinity(), 0x3F800000);
  ok &= expect_bits(-std::numeric_limits<float>::infinity(), 0x00000000);
  ok &= std::isnan(SFUCore::compute(std::numeric_limits<float>::quiet_NaN(),
                                    SFUOp::SIGMOID));

  double max_identity_error = 0.0;
  double max_reversal = 0.0;
  float previous = 0.0f;
  constexpr uint32_t kSamples = 1U << 22;
  for (uint32_t i = 0; i <= kSamples; ++i) {
    float x = -8.0f + 16.0f * static_cast<float>(i) / kSamples;
    float actual = SFUCore::compute(x, SFUOp::SIGMOID);
    float identity = 0.5f * (SFUCore::compute(0.5f * x, SFUOp::TANH) + 1.0f);
    if (i && i != kSamples)
      max_identity_error = std::max(
          max_identity_error, std::abs(static_cast<double>(actual) - identity));
    if (i && actual < previous) {
      max_reversal = std::max(max_reversal,
                              static_cast<double>(previous) - actual);
    }
    previous = actual;
  }

  for (float x : {0.125f, 0.75f, 1.5f, 3.0f, 7.5f}) {
    FilterOutput sigmoid_in = SFUFilter::filter(bits(x), SFUOp::SIGMOID);
    FilterOutput tanh_in = SFUFilter::filter(bits(0.5f * x), SFUOp::TANH);
    RangeReduceOutput sigmoid_rr =
        SFURangeReduce::reduce(sigmoid_in, SFUOp::SIGMOID);
    RangeReduceOutput tanh_rr = SFURangeReduce::reduce(tanh_in, SFUOp::TANH);
    if (sigmoid_rr.index != tanh_rr.index || sigmoid_rr.xl != tanh_rr.xl) {
      std::cerr << "SIGMOID did not reuse TANH reduction at x=" << x << '\n';
      ok = false;
    }
  }

  std::cout << "SIGMOID/TANH identity max absolute difference: "
            << max_identity_error << '\n';
  std::cout << "SIGMOID max quantization reversal: " << max_reversal << '\n';
  ok &= max_identity_error <= 6.0e-8 && max_reversal <= 2.0e-7;
  return ok ? 0 : 1;
}
