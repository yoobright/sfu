#include "sfu_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

struct Interval { const char *name; float begin; float end; };

uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

int main() {
  const std::array<Interval, 5> intervals = {{{"[0,1)",0,1},{"[1,2)",1,2},
    {"[2,4)",2,4},{"[4,6)",4,6},{"[6,8)",6,8}}};
  SFUCore::init();
  bool ok = true;
  std::cout << "Interval   MaxAbsErr    MaxULP\n";
  for (const auto &interval : intervals) {
    double maximum = 0.0;
    uint32_t max_ulp = 0;
    constexpr unsigned kSamples = 1U << 20;
    for (unsigned i=0; i<kSamples; ++i) {
      double t=(static_cast<double>(i)+.5)/kSamples;
      float x=static_cast<float>(interval.begin+(interval.end-interval.begin)*t);
      float actual = SFUCore::compute(x, SFUOp::TANH);
      float reference = static_cast<float>(std::tanh(static_cast<double>(x)));
      maximum = std::max(maximum,
                         std::abs(static_cast<double>(actual) - reference));
      uint32_t actual_bits = bits(actual);
      uint32_t reference_bits = bits(reference);
      max_ulp = std::max(max_ulp, actual_bits > reference_bits
                                      ? actual_bits - reference_bits
                                      : reference_bits - actual_bits);
    }
    std::cout << std::left << std::setw(10) << interval.name << std::scientific
              << std::setprecision(6) << std::setw(13) << maximum
              << std::fixed << max_ulp << '\n';
    ok &= maximum <= 1.2e-6;
  }
  return ok ? 0 : 1;
}
