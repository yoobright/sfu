#ifndef INPUT_GEN_H
#define INPUT_GEN_H

#include "sfu_types.h"
#include <vector>

class InputGen {
public:
  static std::vector<float> generate(SFUOp op, size_t count = 8388608);
  static std::vector<float> generate_range(uint32_t start, uint32_t end);
};

#endif
