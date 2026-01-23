#include "segment_evaluator.h"
#include "sfu_compose.h"
#include "sfu_filter.h"
#include "sfu_poly.h"
#include "sfu_range_reduce.h"
#include "test_utils.h"
#include <cmath>
#include <cstring>
#include <limits>

static constexpr float INV_PI_2 = 2.0f / static_cast<float>(M_PI);

SegmentEvaluator::SegmentEvaluator() {
  // Initialize temp LUT tables with proper sizes
  temp_exp2_lut_.resize(64);
  temp_log2_lut_.resize(64);
  temp_rcp_lut_.resize(128);
  temp_sqrt_even_lut_.resize(64);
  temp_sqrt_odd_lut_.resize(64);
  temp_rsqrt_even_lut_.resize(64);
  temp_rsqrt_odd_lut_.resize(64);
  temp_sin_lut_.resize(64);
}

SegmentEvaluator::~SegmentEvaluator() {}

// Custom LUT lookup with coefficient injection
static LUTOutput lookup_with_coeffs(const RangeReduceOutput &input, SFUOp op,
                                    const LUTEntry &coeffs,
                                    uint32_t segment_idx,
                                    bool is_sqrt_rsqrt_even) {
  LUTOutput out;
  out.xl = input.xl;
  out.sign = input.sign;
  out.exp = input.exp;

  const FunctionParams &params = Function::get(op);

  // For SQRT/RSQRT, we need to check the table selection
  bool use_coeffs = false;
  if (op == SFUOp::SQRT || op == SFUOp::RSQRT) {
    // Check if this segment uses the table we're optimizing
    bool input_uses_even = !(input.index & 0x40);
    use_coeffs = (input_uses_even == is_sqrt_rsqrt_even);

    // Extract the actual segment index within the table
    uint8_t table_segment = input.index & 0x3F;
    use_coeffs = use_coeffs && (table_segment == segment_idx);
  } else {
    // For other ops, check if the segment matches
    uint8_t index = input.index;
    if (op == SFUOp::EXP2 || op == SFUOp::LOG2 || op == SFUOp::SIN ||
        op == SFUOp::COS) {
      index &= 0x3F;
    }
    use_coeffs = (index == segment_idx);
  }

  if (use_coeffs) {
    // Use the provided coefficients
    out.c0 = params.c0_sign * (int32_t)coeffs.c0;
    out.c1 = params.c1_sign * (int32_t)coeffs.c1;
    out.c2 = params.c2_sign * (int32_t)coeffs.c2;
  } else {
    // Fall back to default lookup (we shouldn't hit this in normal use)
    // But to be safe, use zero coefficients
    out.c0 = 0;
    out.c1 = 0;
    out.c2 = 0;
  }

  return out;
}

float SegmentEvaluator::evaluate_single(float input, const LUTEntry &coeffs,
                                        SFUOp op, uint32_t segment_idx,
                                        bool is_sqrt_rsqrt_even) {
  uint32_t input_bits;
  std::memcpy(&input_bits, &input, sizeof(float));

  // Run through pipeline
  FilterOutput filter_out = SFUFilter::filter(input_bits, op);

  if (filter_out.bypass) {
    float result;
    std::memcpy(&result, &filter_out.bypass_val, sizeof(float));
    return result;
  }

  RangeReduceOutput rr_out = SFURangeReduce::reduce(filter_out, op);
  LUTOutput lut_out =
      lookup_with_coeffs(rr_out, op, coeffs, segment_idx, is_sqrt_rsqrt_even);
  PolyOutput poly_out = SFUPoly::compute(lut_out, op);
  uint32_t result_bits = SFUCompose::compose(poly_out, op);

  float result;
  std::memcpy(&result, &result_bits, sizeof(float));
  return result;
}

SegmentMetrics
SegmentEvaluator::evaluate_segment(uint32_t segment_idx, const LUTEntry &coeffs,
                                   const GroundTruthCache &gt_cache, SFUOp op,
                                   bool is_sqrt_rsqrt_even) {
  SegmentMetrics metrics;
  metrics.rms_ulp = 0.0;
  metrics.max_ulp = 0;
  metrics.avg_ulp = 0.0;
  metrics.rms_rel_error = 0.0;
  metrics.max_rel_error = 0.0;
  metrics.avg_rel_error = 0.0;
  metrics.rms_abs_error = 0.0;
  metrics.max_abs_error = 0.0;
  metrics.avg_abs_error = 0.0;
  metrics.worst_input_idx = 0;
  metrics.worst_input_bits = 0;

  uint32_t input_segment_idx = segment_idx;
  if (op == SFUOp::SIN) {
    // Cache inputs for sin are generated in mantissa order over [1,2),
    // while range-reduction maps quadrant-1 inputs to inverted indices.
    input_segment_idx = (gt_cache.get_num_segments() - 1) - segment_idx;
  }

  uint32_t num_samples = gt_cache.get_segment_size(input_segment_idx);
  metrics.num_samples = num_samples;

  double sum_ulp = 0.0;
  double sum_ulp_squared = 0.0;
  double sum_rel_error = 0.0;
  double sum_rel_error_squared = 0.0;
  double sum_abs_error = 0.0;
  double sum_abs_error_squared = 0.0;

  for (uint32_t local_idx = 0; local_idx < num_samples; local_idx++) {
    float input = gt_cache.get_input(input_segment_idx, local_idx);
    float golden = gt_cache.get_ground_truth(input_segment_idx, local_idx);

    float result =
        evaluate_single(input, coeffs, op, segment_idx, is_sqrt_rsqrt_even);

    uint64_t ulp = TestUtils::compute_ulp(golden, result);
    double golden_d = TestUtils::float_to_double(golden);
    double result_d = TestUtils::float_to_double(result);
    double abs_error = TestUtils::compute_abs_error(golden_d, result_d);
    double rel_error = TestUtils::compute_rel_error(golden_d, result_d);

    sum_ulp += ulp;
    sum_ulp_squared += static_cast<double>(ulp) * static_cast<double>(ulp);
    sum_abs_error += abs_error;
    sum_abs_error_squared += abs_error * abs_error;
    sum_rel_error += rel_error;
    sum_rel_error_squared += rel_error * rel_error;

    if (ulp > metrics.max_ulp) {
      metrics.max_ulp = ulp;
      metrics.worst_input_idx = local_idx;
      std::memcpy(&metrics.worst_input_bits, &input, sizeof(float));
    }

    if (abs_error > metrics.max_abs_error) {
      metrics.max_abs_error = abs_error;
    }

    if (rel_error > metrics.max_rel_error) {
      metrics.max_rel_error = rel_error;
    }
  }

  metrics.rms_ulp = std::sqrt(sum_ulp_squared / num_samples);
  metrics.avg_ulp = sum_ulp / num_samples;
  metrics.rms_abs_error = std::sqrt(sum_abs_error_squared / num_samples);
  metrics.avg_abs_error = sum_abs_error / num_samples;
  metrics.rms_rel_error = std::sqrt(sum_rel_error_squared / num_samples);
  metrics.avg_rel_error = sum_rel_error / num_samples;

  return metrics;
}

OverallMetrics
SegmentEvaluator::evaluate_all_segments(const std::vector<LUTEntry> &all_coeffs,
                                        const GroundTruthCache &gt_cache,
                                        SFUOp op, bool is_sqrt_rsqrt_even) {
  OverallMetrics overall;
  overall.total_rms_ulp = 0.0;
  overall.global_max_ulp = 0;
  overall.global_avg_ulp = 0.0;
  overall.total_rms_rel_error = 0.0;
  overall.global_max_rel_error = 0.0;
  overall.global_avg_rel_error = 0.0;
  overall.total_rms_abs_error = 0.0;
  overall.global_max_abs_error = 0.0;
  overall.global_avg_abs_error = 0.0;
  overall.worst_segment_idx = 0;
  overall.total_samples = 0;

  uint32_t num_segments = gt_cache.get_num_segments();
  double sum_ulp_squared_all = 0.0;
  double sum_ulp_all = 0.0;
  double sum_rel_squared_all = 0.0;
  double sum_rel_all = 0.0;
  double sum_abs_squared_all = 0.0;
  double sum_abs_all = 0.0;

  for (uint32_t seg = 0; seg < num_segments; seg++) {
    SegmentMetrics seg_metrics = evaluate_segment(
        seg, all_coeffs[seg], gt_cache, op, is_sqrt_rsqrt_even);

    uint32_t seg_samples = seg_metrics.num_samples;

    sum_ulp_squared_all +=
        seg_metrics.rms_ulp * seg_metrics.rms_ulp * seg_samples;
    sum_ulp_all += seg_metrics.avg_ulp * seg_samples;

    sum_rel_squared_all +=
        seg_metrics.rms_rel_error * seg_metrics.rms_rel_error * seg_samples;
    sum_rel_all += seg_metrics.avg_rel_error * seg_samples;

    sum_abs_squared_all +=
        seg_metrics.rms_abs_error * seg_metrics.rms_abs_error * seg_samples;
    sum_abs_all += seg_metrics.avg_abs_error * seg_samples;

    overall.total_samples += seg_samples;

    if (seg_metrics.max_ulp > overall.global_max_ulp) {
      overall.global_max_ulp = seg_metrics.max_ulp;
      overall.worst_segment_idx = seg;
    }

    if (seg_metrics.max_rel_error > overall.global_max_rel_error) {
      overall.global_max_rel_error = seg_metrics.max_rel_error;
    }

    if (seg_metrics.max_abs_error > overall.global_max_abs_error) {
      overall.global_max_abs_error = seg_metrics.max_abs_error;
    }
  }

  overall.total_rms_ulp =
      std::sqrt(sum_ulp_squared_all / overall.total_samples);
  overall.global_avg_ulp = sum_ulp_all / overall.total_samples;
  overall.total_rms_rel_error =
      std::sqrt(sum_rel_squared_all / overall.total_samples);
  overall.global_avg_rel_error = sum_rel_all / overall.total_samples;
  overall.total_rms_abs_error =
      std::sqrt(sum_abs_squared_all / overall.total_samples);
  overall.global_avg_abs_error = sum_abs_all / overall.total_samples;

  return overall;
}
