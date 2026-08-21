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
  uint32_t begin;
  uint32_t end;
  bool reduced;
};

struct AccuracyStats {
  uint64_t samples = 0;
  double max_abs_error = 0.0;
  double sum_abs_error = 0.0;
  uint64_t max_ulp = 0;
  uint64_t sum_ulp = 0;
};

float from_bits(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float sigmoid_reference(float x) {
  return static_cast<float>(1.0 / (1.0 + std::exp(-static_cast<double>(x))));
}

uint32_t float_bits(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

AccuracyStats measure(const Interval &interval) {
  AccuracyStats stats;
  for (uint32_t value = interval.begin; value < interval.end; ++value) {
    float input = interval.reduced ? static_cast<float>(value) * 0x1p-19f
                                   : from_bits(value);
    float actual = SFUCore::compute(input, SFUOp::SIGMOID);
    float reference = sigmoid_reference(input);
    double abs_error =
        std::abs(static_cast<double>(actual) - static_cast<double>(reference));
    uint32_t actual_bits = float_bits(actual);
    uint32_t reference_bits = float_bits(reference);
    uint64_t ulp = actual_bits > reference_bits
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
  constexpr std::array<Interval, 6> kIntervals = {{
      {"[0, 0.25)", 0, 1U << 17, true},
      {"[0.25, 0.5)", 0x3E800000, 0x3F000000, false},
      {"[0.5, 1)", 0x3F000000, 0x3F800000, false},
      {"[1, 2)", 0x3F800000, 0x40000000, false},
      {"[2, 4)", 0x40000000, 0x40800000, false},
      {"[4, 6)", 0x40800000, 0x40C00000, false},
  }};

  SFUCore::init();
  std::cout << "Interval       Samples  MaxAbsErr    MaxULP  AvgAbsErr    AvgULP\n";
  for (const Interval &interval : kIntervals) {
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
  }
  return 0;
}
