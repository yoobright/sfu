#ifndef CPU_H
#define CPU_H

#include "sfu_types.h"
#include <cstddef>

class CPU {
public:
  static float compute(float input, SFUOp op);
  static void compute_batch(const float *inputs, float *outputs, size_t n,
                            SFUOp op);
};

#endif
