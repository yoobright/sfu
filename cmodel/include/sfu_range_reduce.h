#ifndef SFU_RANGE_REDUCE_H
#define SFU_RANGE_REDUCE_H

#include "sfu_types.h"

class SFURangeReduce {
public:
  static void configure_sigmoid(uint32_t lut_entries);
  static RangeReduceOutput reduce(const FilterOutput &input, SFUOp op);

private:
  static uint32_t sigmoid_lut_entries;
};

#endif
