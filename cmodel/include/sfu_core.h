#ifndef SFU_CORE_H
#define SFU_CORE_H

#include "sfu_types.h"
#include <cstdint>

class SFUCore {
public:
  static void init();
  static float compute(float input, SFUOp op);
};

#endif
