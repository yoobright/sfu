#include "sfu_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>

struct Interval { const char *name; float begin; float end; };
int main() {
  const std::array<Interval, 5> intervals = {{{"[0,1)",0,1},{"[1,2)",1,2},
    {"[2,4)",2,4},{"[4,6)",4,6},{"[6,8)",6,8}}};
  SFUCore::init();
  bool ok = true;
  std::cout << "Interval   MaxAbsErr\n";
  for (const auto &interval : intervals) {
    double maximum = 0.0;
    constexpr unsigned kSamples = 1U << 20;
    for (unsigned i=0; i<kSamples; ++i) {
      double t=(static_cast<double>(i)+.5)/kSamples;
      float x=static_cast<float>(interval.begin+(interval.end-interval.begin)*t);
      maximum=std::max(maximum,std::abs(static_cast<double>(SFUCore::compute(x,SFUOp::TANH))-std::tanh(static_cast<double>(x))));
    }
    std::cout << std::left << std::setw(10) << interval.name << std::scientific << std::setprecision(6) << maximum << '\n';
    ok &= maximum <= 1.2e-6;
  }
  return ok ? 0 : 1;
}
