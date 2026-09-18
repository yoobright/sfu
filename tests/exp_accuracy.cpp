#include "sfu_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {

struct Interval {
  const char *name;
  float begin;
  float end;
};

struct AccuracyStats {
  uint64_t samples = 0;
  double max_abs_error = 0.0;
  double sum_abs_error = 0.0;
  uint32_t max_ulp = 0;
  uint64_t sum_ulp = 0;
};

uint32_t float_bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

AccuracyStats measure(const Interval &interval) {
  constexpr uint32_t kSamples = 1U << 20;
  AccuracyStats stats;
  for (uint32_t i = 0; i < kSamples; ++i) {
    double t = (static_cast<double>(i) + 0.5) / kSamples;
    float input = static_cast<float>(interval.begin +
                                     (interval.end - interval.begin) * t);
    float actual = SFUCore::compute(input, SFUOp::EXP);
    float reference = static_cast<float>(std::exp(static_cast<double>(input)));
    double abs_error = std::abs(static_cast<double>(actual) - reference);
    uint32_t actual_bits = float_bits(actual);
    uint32_t reference_bits = float_bits(reference);
    uint32_t ulp = actual_bits > reference_bits
                       ? actual_bits - reference_bits
                       : reference_bits - actual_bits;

    ++stats.samples;
    stats.max_abs_error = std::max(stats.max_abs_error, abs_error);
    stats.sum_abs_error += abs_error;
    stats.max_ulp = std::max(stats.max_ulp, ulp);
    stats.sum_ulp += ulp;
  }
  return stats;
}

} // namespace

int main() {
  const std::array<Interval, 10> intervals = {{{"[-104, -87.34)", -104.0f, -87.33655f},
    {"[-87.34, -16)", -87.33655f, -16.0f},
    {"[-16, -8)", -16.0f, -8.0f}, {"[-8, -4)", -8.0f, -4.0f},
    {"[-4, -2)", -4.0f, -2.0f}, {"[-2, -1)", -2.0f, -1.0f},
    {"[-1, 0)", -1.0f, 0.0f}, {"[0, 16)", 0.0f, 16.0f},
    {"[16, 64)", 16.0f, 64.0f}, {"[64, 88.72]", 64.0f, 88.72283172607421875f}}};

  SFUCore::init();
  bool ok = true;
  std::cout << "Interval       Samples  MaxAbsErr    MaxULP  AvgAbsErr    AvgULP\n";
  for (const Interval &interval : intervals) {
    AccuracyStats stats = measure(interval);
    std::cout << std::left << std::setw(14) << interval.name << std::right
              << std::setw(9) << stats.samples << "  " << std::scientific
              << std::setprecision(6) << std::setw(12) << stats.max_abs_error
              << "  " << std::fixed << std::setprecision(0) << std::setw(6)
              << stats.max_ulp << "  " << std::scientific
              << std::setprecision(6) << std::setw(12)
              << stats.sum_abs_error / stats.samples << "  " << std::fixed
              << std::setprecision(2) << std::setw(6)
              << static_cast<double>(stats.sum_ulp) / stats.samples << '\n';
    ok &= stats.max_ulp <= 2;
  }
  return ok ? 0 : 1;
}
