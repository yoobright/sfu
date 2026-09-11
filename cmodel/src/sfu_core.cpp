#include "sfu_core.h"
#include "sfu_compose.h"
#include "sfu_filter.h"
#include "sfu_lut.h"
#include "sfu_poly.h"
#include "sfu_range_reduce.h"
#include <cmath>
#include <cstring>

static constexpr float INV_PI_2 = 2.0f / static_cast<float>(M_PI);

void SFUCore::init() { SFULUT::init(); }

float SFUCore::compute(float input, SFUOp op) {

  if (op == SFUOp::SIN || op == SFUOp::COS) {
    input = input * INV_PI_2;
  }

  uint32_t input_bits;
  memcpy(&input_bits, &input, sizeof(float));

  FilterOutput filter_out = SFUFilter::filter(input_bits, op);

  if (filter_out.bypass) {
    float result;
    memcpy(&result, &filter_out.bypass_val, sizeof(float));
    return result;
  }

  RangeReduceOutput rr_out = SFURangeReduce::reduce(filter_out, op);
  LUTOutput lut_out = SFULUT::lookup(rr_out, op);
  PolyOutput poly_out = SFUPoly::compute(lut_out, op);
  uint32_t result_bits = SFUCompose::compose(poly_out, op);

  float result;
  memcpy(&result, &result_bits, sizeof(float));
  return result;
}
