#include "sfu_core.h"
#ifdef TEST_EXP_RTL
#include "chisel.h"
#endif
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

bool rtl_ok = true;
float evaluate(float x) {
  float result = SFUCore::compute(x, SFUOp::EXP);
#ifdef TEST_EXP_RTL
  float rtl = Chisel::compute(x, SFUOp::EXP);
  if (bits(rtl) != bits(result)) {
    if (rtl_ok)
      std::cerr << "EXP RTL mismatch at " << x << '\n';
    rtl_ok = false;
  }
#endif
  return result;
}

bool expect_bits(float input, uint32_t expected) {
  float actual = evaluate(input);
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
#ifdef TEST_EXP_RTL
  Chisel::init();
#endif
  bool ok = true;

  ok &= expect_bits(0.0f, 0x3F800000);
  ok &= expect_bits(-0.0f, 0x3F800000);
  ok &= expect_bits(std::numeric_limits<float>::infinity(), 0x7F800000);
  ok &= expect_bits(-std::numeric_limits<float>::infinity(), 0x00000000);
  ok &= expect_bits(std::numeric_limits<float>::max(), 0x7F800000);
  ok &= expect_bits(-std::numeric_limits<float>::max(), 0x00000000);
  ok &= expect_bits(std::numeric_limits<float>::denorm_min(), 0x3F800000);
  ok &= expect_bits(-std::numeric_limits<float>::denorm_min(), 0x3F800000);
  ok &= expect_bits(88.72283935546875f, 0x7F800000);
  ok &= expect_bits(-103.97208404541015625f, 0x00000000);

  float nan = evaluate(std::numeric_limits<float>::quiet_NaN());
  if (!std::isnan(nan)) {
    std::cerr << "EXP NaN input did not produce NaN\n";
    ok = false;
  }

  // Adjacent floats around overflow, zero underflow, normal/subnormal,
  // the old clipping limits, and every integer/segment reduction boundary.
  auto check = [&](float x) {
    float actual = evaluate(x);
    float ref = static_cast<float>(std::exp(static_cast<double>(x)));
    if (!std::isfinite(actual) || ulp_distance(actual, ref) > 2 ||
        (ref > 0.0f && actual == 0.0f)) {
      std::cerr << "EXP boundary failure x=" << x << " actual=" << actual
                << " reference=" << ref << '\n';
      ok = false;
    }
  };
  for (float x : {-103.972076416015625f, -100.0f, -90.0f,
                  -87.3365478515625f, -17.0f, -16.0f, 0.0f, 1.0f,
                  16.0f, 80.0f, 88.72283172607421875f}) {
    check(x);
    check(std::nextafter(x, 0.0f));
  }
  for (int i = -149 * 64; i < 127 * 64; ++i) {
    float x = static_cast<float>((i / 64.0) * std::log(2.0));
    check(x);
    check(std::nextafter(x, -std::numeric_limits<float>::infinity()));
    check(std::nextafter(x, std::numeric_limits<float>::infinity()));
  }

  uint32_t max_ulp = 0;
  float previous = 0.0f;
  constexpr uint32_t kSamples = 1U << 22;
  for (uint32_t i = 0; i <= kSamples; ++i) {
    float input = static_cast<float>(-103.972076416015625 +
        192.69490814208984375 * static_cast<double>(i) / kSamples);
    float actual = evaluate(input);
    float reference = static_cast<float>(std::exp(static_cast<double>(input)));
    max_ulp = std::max(max_ulp, ulp_distance(actual, reference));
    if (i != 0 && actual < previous) {
      std::cerr << "EXP monotonicity failure at x=" << input << '\n';
      ok = false;
      break;
    }
    previous = actual;
  }

  std::cout << "EXP full finite-output range sampled max ULP: " << max_ulp << '\n';
  if (max_ulp > 2) {
    std::cerr << "EXP accuracy limit exceeded\n";
    ok = false;
  }

  // Deterministic raw-bit sampling covers tiny inputs and both tails.
  uint32_t state = 0x65A4E123;
  for (unsigned i = 0; i < (1U << 20); ++i) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    float x;
    std::memcpy(&x, &state, sizeof(x));
    float actual = evaluate(x);
    float ref = static_cast<float>(std::exp(static_cast<double>(x)));
    if (std::isnan(ref)) {
      ok &= std::isnan(actual);
    } else if (std::isinf(ref) || ref == 0.0f) {
      ok &= bits(actual) == bits(ref);
    } else {
      ok &= std::isfinite(actual) && ulp_distance(actual, ref) <= 2;
    }
  }
#ifdef TEST_EXP_RTL
  Chisel::cleanup();
#endif
  return ok && rtl_ok ? 0 : 1;
}
