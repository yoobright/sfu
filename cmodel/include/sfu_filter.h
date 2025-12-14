#ifndef SFU_FILTER_H
#define SFU_FILTER_H

#include "sfu_types.h"

class SFUFilter {
public:
  static FilterOutput filter(uint32_t input_bits, SFUOp op);
};

#endif
