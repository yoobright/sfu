#ifndef CHISEL_H
#define CHISEL_H

#include "sfu_types.h"
#include <cstddef>

class Chisel {
public:
  static void init();
  static void cleanup();
  static float compute(float input, SFUOp op);
  static void compute_batch(const float *inputs, float *outputs, size_t n,
                            SFUOp op);
};

#endif
