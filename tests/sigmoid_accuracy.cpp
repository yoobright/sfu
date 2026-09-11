#include "sfu_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {
struct Interval { const char *name; float begin; float end; };
struct Stats { double max_abs = 0.0; uint32_t max_ulp = 0; };
uint32_t bits(float x) { uint32_t b; std::memcpy(&b, &x, 4); return b; }

Stats measure(const Interval &interval) {
  constexpr uint32_t kSamples = 1U << 20;
  Stats stats;
  for (uint32_t i = 0; i < kSamples; ++i) {
    double t = (static_cast<double>(i) + 0.5) / kSamples;
    float x = static_cast<float>(interval.begin + (interval.end-interval.begin)*t);
    float actual = SFUCore::compute(x, SFUOp::SIGMOID);
    float reference = static_cast<float>(0.5 * (std::tanh(0.5 * static_cast<double>(x)) + 1.0));
    stats.max_abs = std::max(stats.max_abs, std::abs(static_cast<double>(actual)-reference));
    uint32_t a = bits(actual), r = bits(reference);
    stats.max_ulp = std::max(stats.max_ulp, a > r ? a-r : r-a);
  }
  return stats;
}
} // namespace

int main() {
  const std::array<Interval, 6> intervals = {{{"[0,.5)",0,.5f},{"[.5,1)",.5f,1},
    {"[1,2)",1,2},{"[2,4)",2,4},{"[4,6)",4,6},{"[6,8)",6,8}}};
  SFUCore::init();
  bool ok = true;
  std::cout << "Interval   MaxAbsErr    MaxULP\n";
  for (const auto &interval : intervals) {
    Stats stats = measure(interval);
    std::cout << std::left << std::setw(10) << interval.name << std::scientific
              << std::setprecision(6) << std::setw(13) << stats.max_abs
              << std::fixed << stats.max_ulp << '\n';
    ok &= stats.max_abs <= 6.0e-7;
  }
  return ok ? 0 : 1;
}
