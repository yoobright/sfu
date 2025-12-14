#ifndef SFU_RANGE_REDUCE_H
#define SFU_RANGE_REDUCE_H

#include "sfu_types.h"

class SFURangeReduce {
public:
  static RangeReduceOutput reduce(const FilterOutput &input, SFUOp op);
};

#endif
