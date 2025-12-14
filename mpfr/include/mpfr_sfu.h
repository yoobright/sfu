#ifndef MPFR_SFU_H
#define MPFR_SFU_H

#include "sfu_types.h"
#include <cstddef>

class MPFR {
public:
  static float compute_float(float input, SFUOp op);
  static double compute_double(double input, SFUOp op);
  static void compute_batch_float(const float *inputs, float *outputs, size_t n,
                                  SFUOp op);
  static void compute_batch_double(const double *inputs, double *outputs,
                                   size_t n, SFUOp op);
};

#endif
