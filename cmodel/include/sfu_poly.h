#ifndef SFU_POLY_H
#define SFU_POLY_H

#include "sfu_types.h"

class SFUPoly {
public:
  static PolyOutput compute(const LUTOutput &input, SFUOp op);
};

#endif
