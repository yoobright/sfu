#ifndef SFU_COMPOSE_H
#define SFU_COMPOSE_H

#include "sfu_types.h"

class SFUCompose {
public:
  static uint32_t compose(const PolyOutput &input, SFUOp op);
};

#endif
