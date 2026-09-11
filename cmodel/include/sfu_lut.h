#ifndef SFU_LUT_H
#define SFU_LUT_H

#include "sfu_types.h"
#include <vector>

struct LUTEntry {
  uint32_t c0;
  uint32_t c1;
  uint32_t c2;
};

class SFULUT {
public:
  static void init();
  static LUTOutput lookup(const RangeReduceOutput &input, SFUOp op);

private:
  static std::vector<LUTEntry> load_lut(const char *filename, int num_entries);

  static std::vector<LUTEntry> exp2_lut;
  static std::vector<LUTEntry> log2_lut;
  static std::vector<LUTEntry> rcp_lut;
  static std::vector<LUTEntry> sqrt_even_lut;
  static std::vector<LUTEntry> sqrt_odd_lut;
  static std::vector<LUTEntry> rsqrt_even_lut;
  static std::vector<LUTEntry> rsqrt_odd_lut;
  static std::vector<LUTEntry> sin_lut;
  static std::vector<LUTEntry> tanh_lut;
};

#endif
