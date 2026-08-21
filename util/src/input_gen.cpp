#include "input_gen.h"
#include <cstring>

std::vector<float> InputGen::generate(SFUOp op, size_t count) {
  if (op == SFUOp::SIGMOID) {
    constexpr size_t kDefaultCount = 6U << 20;
    if (count == 0 || count > kDefaultCount)
      count = kDefaultCount;

    std::vector<float> inputs;
    inputs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      double t = count == 1 ? 0.0 : static_cast<double>(i) / (count - 1);
      inputs.push_back(static_cast<float>(-6.0 + 12.0 * t));
    }
    return inputs;
  }

  uint32_t start, end;

  switch (op) {
  case SFUOp::EXP2:
    start = 0x3F800000;
    end = 0x40000000;
    break;

  case SFUOp::LOG2:
    start = 0x3F800000;
    end = 0x40000000;
    break;

  case SFUOp::RCP:
    start = 0x3F800000;
    end = 0x40000000;
    break;

  case SFUOp::SQRT:
    start = 0x3E800000;
    end = 0x3F800000;
    break;

  case SFUOp::RSQRT:
    start = 0x3F800000;
    end = 0x40800000;
    break;

  default:
    start = 0x3F800000;
    end = 0x40000000;
  }

  if (count == 0) {
    count = end - start;
  } else {
    count = (count < (end - start)) ? count : (end - start);
  }

  return generate_range(start, start + count);
}

std::vector<float> InputGen::generate_range(uint32_t start, uint32_t end) {
  std::vector<float> inputs;
  inputs.reserve(end - start);

  for (uint32_t bits = start; bits < end; bits++) {
    float value;
    memcpy(&value, &bits, sizeof(float));
    inputs.push_back(value);
  }

  return inputs;
}
