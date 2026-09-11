#ifndef SEGMENT_EVALUATOR_H
#define SEGMENT_EVALUATOR_H

#include "optimizer_types.h"
#include "ground_truth_cache.h"
#include "sfu_lut.h"
#include <vector>

class SegmentEvaluator {
public:
    SegmentEvaluator();
    ~SegmentEvaluator();

    // Evaluate a segment with given coefficients
    SegmentMetrics evaluate_segment(
        uint32_t segment_idx,
        const LUTEntry& coeffs,
        const GroundTruthCache& gt_cache,
        SFUOp op,
        bool is_sqrt_rsqrt_even = true
    );

    // Evaluate all segments (for validation)
    OverallMetrics evaluate_all_segments(
        const std::vector<LUTEntry>& all_coeffs,
        const GroundTruthCache& gt_cache,
        SFUOp op,
        bool is_sqrt_rsqrt_even = true
    );

private:
    // Evaluate single input with custom coefficients
    float evaluate_single(
        float input,
        const LUTEntry& coeffs,
        SFUOp op,
        uint32_t segment_idx,
        bool is_sqrt_rsqrt_even
    );

    // Temporary LUT tables for coefficient injection
    std::vector<LUTEntry> temp_exp2_lut_;
    std::vector<LUTEntry> temp_log2_lut_;
    std::vector<LUTEntry> temp_rcp_lut_;
    std::vector<LUTEntry> temp_sqrt_even_lut_;
    std::vector<LUTEntry> temp_sqrt_odd_lut_;
    std::vector<LUTEntry> temp_rsqrt_even_lut_;
    std::vector<LUTEntry> temp_rsqrt_odd_lut_;
    std::vector<LUTEntry> temp_sin_lut_;
    std::vector<LUTEntry> temp_tanh_lut_;
};

#endif // SEGMENT_EVALUATOR_H
