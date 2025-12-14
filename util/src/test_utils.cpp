#include "test_utils.h"
#include <cstring>

uint64_t TestUtils::compute_ulp(float golden, float test) {
  uint32_t g_bits, t_bits;
  memcpy(&g_bits, &golden, sizeof(float));
  memcpy(&t_bits, &test, sizeof(float));

  bool g_nan = std::isnan(golden);
  bool t_nan = std::isnan(test);
  bool g_inf = std::isinf(golden);
  bool t_inf = std::isinf(test);

  if (g_nan && t_nan)
    return 0;
  if (g_inf && t_inf && ((g_bits & 0x80000000) == (t_bits & 0x80000000)))
    return 0;
  if (golden == test)
    return 0;

  bool g_neg = (g_bits >> 31) & 1;
  bool t_neg = (t_bits >> 31) & 1;

  if (g_neg != t_neg) {
    uint32_t g_mag = g_bits & 0x7FFFFFFF;
    uint32_t t_mag = t_bits & 0x7FFFFFFF;
    return (uint64_t)g_mag + (uint64_t)t_mag;
  }

  return (g_bits > t_bits) ? (uint64_t)(g_bits - t_bits)
                           : (uint64_t)(t_bits - g_bits);
}

double TestUtils::float_to_double(float x) { return static_cast<double>(x); }

double TestUtils::compute_abs_error(double golden, double test) {
  return std::fabs(test - golden);
}

double TestUtils::compute_rel_error(double golden, double test) {
  if (golden == 0.0)
    return 0.0;
  return std::fabs((test - golden) / golden);
}
