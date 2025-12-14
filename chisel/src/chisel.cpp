#include "chisel.h"
#include "VSFU.h"
#include "verilated.h"
#include <cstring>

static VSFU *top = nullptr;
static VerilatedContext *contextp = nullptr;

void Chisel::init() {
  contextp = new VerilatedContext;
  top = new VSFU(contextp);
  top->reset = 1;
  top->clock = 0;
  top->eval();
  top->clock = 1;
  top->eval();
  top->reset = 0;
}

void Chisel::cleanup() {
  if (top) {
    top->final();
    delete top;
    top = nullptr;
  }
  if (contextp) {
    delete contextp;
    contextp = nullptr;
  }
}

float Chisel::compute(float input, SFUOp op) {
  uint32_t input_bits;
  memcpy(&input_bits, &input, sizeof(float));

  top->io_in_valid = 1;
  top->io_in_bits_x = input_bits;
  top->io_in_bits_op = static_cast<uint8_t>(op);
  top->io_out_ready = 1;

  top->clock = 0;
  top->eval();
  top->clock = 1;
  top->eval();

  top->io_in_valid = 0;

  int timeout = 1000;
  while (!top->io_out_valid && timeout-- > 0) {
    top->clock = 0;
    top->eval();
    top->clock = 1;
    top->eval();
  }

  uint32_t result_bits = top->io_out_bits_result;
  float result;
  memcpy(&result, &result_bits, sizeof(float));

  return result;
}

void Chisel::compute_batch(const float *inputs, float *outputs, size_t n,
                           SFUOp op) {
  uint32_t input_bits = 0;
  float result = 0.0f;
  size_t issued = 0;
  size_t received = 0;

  top->io_in_valid = 0;
  top->io_out_ready = 1;

  uint8_t opcode = static_cast<uint8_t>(op);

  while (received < n) {
    if (issued < n) {
      memcpy(&input_bits, &inputs[issued], sizeof(float));

      top->io_in_valid = 1;
      top->io_in_bits_x = input_bits;
      top->io_in_bits_op = opcode;

      issued++;
    } else {
      top->io_in_valid = 0;
    }

    top->clock = 0;
    top->eval();
    top->clock = 1;
    top->eval();

    if (top->io_out_valid) {
      memcpy(&result, &top->io_out_bits_result, sizeof(float));

      outputs[received] = result;
      received++;
    }
  }

  top->io_in_valid = 0;
}
