#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <cmath>
#include <cstdint>

class TestUtils {
public:
  static uint64_t compute_ulp(float golden, float test);
  static double float_to_double(float x);
  static double compute_abs_error(double golden, double test);
  static double compute_rel_error(double golden, double test);
};

#endif
