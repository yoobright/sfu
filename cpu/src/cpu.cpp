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
    if (input <= -8.0f)
      return 0.0f;
    if (input >= 8.0f)
      return 1.0f;
    return 0.5f * (tanhf(0.5f * input) + 1.0f);
  case SFUOp::EXP:
    if (std::isnan(input))
      return input;
    if (input < -16.0f)
      return 0.0f;
    if (input > 0.0f)
      return 1.0f;
    return expf(input);
  case SFUOp::TANH:
    if (input <= -8.0f)
      return -1.0f;
    if (input >= 8.0f)
      return 1.0f;
    return tanhf(input);
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
