#include "gpu.h"
#include <cstring>
#include <cuda_runtime.h>

static float *d_inputs = nullptr;
static float *d_outputs = nullptr;
static size_t allocated_size = 0;

__global__ void sfu_kernel(const float *inputs, float *outputs, int n, int op) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= n)
    return;

  float input = inputs[idx];
  float result;

  switch (op) {
  case 0:
    result = exp2f(input);
    break;
  case 1:
    result = log2f(input);
    break;
  case 2:
    result = 1.0f / input;
    break;
  case 3:
    result = sqrtf(input);
    break;
  case 4:
    result = rsqrtf(input);
    break;
  default:
    result = 0.0f;
  }

  outputs[idx] = result;
}

void GPU::init() {
  allocated_size = 8388608;
  cudaMalloc(&d_inputs, allocated_size * sizeof(float));
  cudaMalloc(&d_outputs, allocated_size * sizeof(float));
}

void GPU::cleanup() {
  if (d_inputs) {
    cudaFree(d_inputs);
    d_inputs = nullptr;
  }
  if (d_outputs) {
    cudaFree(d_outputs);
    d_outputs = nullptr;
  }
  allocated_size = 0;
}

float GPU::compute(float input, SFUOp op) {
  float output;
  compute_batch(&input, &output, 1, op);
  return output;
}

void GPU::compute_batch(const float *inputs, float *outputs, size_t n,
                        SFUOp op) {
  if (n > allocated_size) {
    if (d_inputs)
      cudaFree(d_inputs);
    if (d_outputs)
      cudaFree(d_outputs);
    allocated_size = n;
    cudaMalloc(&d_inputs, allocated_size * sizeof(float));
    cudaMalloc(&d_outputs, allocated_size * sizeof(float));
  }

  cudaMemcpy(d_inputs, inputs, n * sizeof(float), cudaMemcpyHostToDevice);

  int block_size = 256;
  int grid_size = (n + block_size - 1) / block_size;
  sfu_kernel<<<grid_size, block_size>>>(d_inputs, d_outputs, n,
                                        static_cast<int>(op));

  cudaMemcpy(outputs, d_outputs, n * sizeof(float), cudaMemcpyDeviceToHost);
}
