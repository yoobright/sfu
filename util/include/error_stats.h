#ifndef ERROR_STATS_H
#define ERROR_STATS_H

#include <cstddef>
#include <cstdint>

struct ErrorStats {
  size_t total;
  size_t pass_count;
  size_t fail_count;

  double avg_abs_err;
  double max_abs_err;
  double avg_rel_err;
  double max_rel_err;

  double avg_ulp;
  uint64_t max_ulp;
};

class ErrorComputer {
public:
  static ErrorStats compute_stats(const float *golden, const float *test,
                                  size_t n, double abs_threshold = 1e-7,
                                  double rel_threshold = 1e-6,
                                  uint64_t ulp_threshold = 2);

  static void print_stats(const char *name, const ErrorStats &stats);
};

#endif
