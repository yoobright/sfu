#include "sfu_core.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
uint32_t bits(float x) { uint32_t b; std::memcpy(&b, &x, 4); return b; }
bool expect(float x, uint32_t expected) {
  uint32_t actual = bits(SFUCore::compute(x, SFUOp::TANH));
  if (actual == expected) return true;
  std::cerr << "TANH special case failed at " << x << ": 0x" << std::hex
            << actual << " != 0x" << expected << std::dec << '\n';
  return false;
}
} // namespace

int main() {
  SFUCore::init();
  bool ok = true;
  ok &= expect(0.0f, 0x00000000);
  ok &= expect(-0.0f, 0x80000000);
  ok &= expect(8.0f, 0x3F800000);
  ok &= expect(-8.0f, 0xBF800000);
  ok &= expect(std::numeric_limits<float>::infinity(), 0x3F800000);
  ok &= expect(-std::numeric_limits<float>::infinity(), 0xBF800000);
  ok &= std::isnan(SFUCore::compute(std::numeric_limits<float>::quiet_NaN(), SFUOp::TANH));

  double max_abs = 0.0;
  double max_reversal = 0.0;
  float previous = -1.0f;
  constexpr uint32_t kSamples = 1U << 22;
  for (uint32_t i = 0; i <= kSamples; ++i) {
    float x = -8.0f + 16.0f * static_cast<float>(i) / kSamples;
    float actual = SFUCore::compute(x, SFUOp::TANH);
    max_abs = std::max(max_abs, std::abs(static_cast<double>(actual)-std::tanh(static_cast<double>(x))));
    if (i && actual < previous)
      max_reversal = std::max(max_reversal,
                              static_cast<double>(previous) - actual);
    previous = actual;
  }
  std::cout << "TANH [-8,8] sampled max absolute error: " << max_abs << '\n';
  std::cout << "TANH max quantization reversal: " << max_reversal << '\n';
  ok &= max_abs <= 1.0e-6 && max_reversal <= 4.0e-7;
  return ok ? 0 : 1;
}
