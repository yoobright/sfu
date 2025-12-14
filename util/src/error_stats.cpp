#include "error_stats.h"
#include "test_utils.h"
#include <cstdio>

ErrorStats ErrorComputer::compute_stats(const float *golden, const float *test,
                                        size_t n, double abs_threshold,
                                        double rel_threshold,
                                        uint64_t ulp_threshold) {
  ErrorStats stats = {0};
  stats.total = n;

  double sum_abs_err = 0.0;
  double sum_rel_err = 0.0;
  uint64_t sum_ulp = 0;

  for (size_t i = 0; i < n; i++) {
    double g = TestUtils::float_to_double(golden[i]);
    double t = TestUtils::float_to_double(test[i]);

    double abs_err = TestUtils::compute_abs_error(g, t);
    double rel_err = TestUtils::compute_rel_error(g, t);
    uint64_t ulp = TestUtils::compute_ulp(golden[i], test[i]);

    sum_abs_err += abs_err;
    sum_rel_err += rel_err;
    sum_ulp += ulp;

    if (abs_err > stats.max_abs_err)
      stats.max_abs_err = abs_err;
    if (rel_err > stats.max_rel_err)
      stats.max_rel_err = rel_err;
    if (ulp > stats.max_ulp)
      stats.max_ulp = ulp;

    bool pass = (abs_err <= abs_threshold || rel_err <= rel_threshold) &&
                (ulp <= ulp_threshold);
    if (pass) {
      stats.pass_count++;
    } else {
      stats.fail_count++;
    }
  }

  stats.avg_abs_err = sum_abs_err / n;
  stats.avg_rel_err = sum_rel_err / n;
  stats.avg_ulp = static_cast<double>(sum_ulp) / n;

  return stats;
}

void ErrorComputer::print_stats(const char *name, const ErrorStats &stats) {
  printf("=== %s Statistics ===\n", name);
  printf("Total: %zu, Pass: %zu (%.2f%%), Fail: %zu (%.2f%%)\n", stats.total,
         stats.pass_count, 100.0 * stats.pass_count / stats.total,
         stats.fail_count, 100.0 * stats.fail_count / stats.total);
  printf("AvgAbsErr: %.6e, MaxAbsErr: %.6e\n", stats.avg_abs_err,
         stats.max_abs_err);
  printf("AvgRelErr: %.6e, MaxRelErr: %.6e\n", stats.avg_rel_err,
         stats.max_rel_err);
  printf("AvgULP: %.2lf, MaxULP: %lu\n", stats.avg_ulp, stats.max_ulp);
  printf("\n");
}
