#include "cpu.h"
#include <cmath>

float CPU::compute(float input, SFUOp op) {
  switch (op) {
  case SFUOp::EXP2:
    return exp2f(input);
  case SFUOp::LOG2:
    return log2f(input);
  case SFUOp::RCP:
    return 1.0f / input;
  case SFUOp::SQRT:
    return sqrtf(input);
  case SFUOp::RSQRT:
    return 1.0f / sqrtf(input);
  case SFUOp::SIN:
    return sinf(input);
  case SFUOp::COS:
    return cosf(input);
  case SFUOp::SIGMOID:
    if (input >= 0.0f)
      return 1.0f / (1.0f + expf(-input));
    {
      float exp_x = expf(input);
      return exp_x / (1.0f + exp_x);
    }
  default:
    return 0.0f;
  }
}

void CPU::compute_batch(const float *inputs, float *outputs, size_t n,
                        SFUOp op) {
  for (size_t i = 0; i < n; i++) {
    outputs[i] = compute(inputs[i], op);
  }
}
