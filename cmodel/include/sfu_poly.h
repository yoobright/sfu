#ifndef SFU_POLY_H
#define SFU_POLY_H

#include "sfu_types.h"

class SFUPoly {
public:
  static void configure_sigmoid(uint32_t lut_entries);
  static PolyOutput compute(const LUTOutput &input, SFUOp op);

private:
  static uint32_t sigmoid_lut_entries;
};

#endif
