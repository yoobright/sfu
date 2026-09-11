#ifndef OPTIMIZER_TYPES_H
#define OPTIMIZER_TYPES_H

#include <cstdint>
#include <vector>
#include <string>
#include "sfu_types.h"
#include "sfu_lut.h"

enum class OptimizationMetric {
    RMS_ULP,
    MAX_ULP,
    RMS_REL_ERROR,
    MAX_REL_ERROR,
    RMS_ABS_ERROR,
    MAX_ABS_ERROR,
    HYBRID_ULP_MAX_AVG,
    HYBRID_ABS_MAX_AVG,
    HYBRID_REL_MAX_AVG
};

struct CoeffOptimizationConfig {
    bool optimize_c0;
    bool optimize_c1;
    bool optimize_c2;
    int c0_search_range;
    int c1_search_range;
    int c2_search_range;
    OptimizationMetric primary_metric;
    OptimizationMetric secondary_metric;
};

struct GroundTruthHeader {
    uint32_t magic;              // 0x47544348 "GTCH"
    uint32_t function_op;        // SFUOp enum value
    uint32_t num_samples;        // Total samples
    uint32_t num_segments;       // 2^m
    uint8_t m_bits;              // Segmentation parameter
    float range_start;           // Input range start
    float range_end;             // Input range end
    uint32_t reserved[8];        // Future expansion
};

struct GroundTruthEntry {
    uint32_t input_bits;         // Float as uint32_t
    uint32_t output_bits;        // MPFR result as uint32_t
};

struct SegmentMetrics {
    double rms_ulp;
    uint64_t max_ulp;
    double avg_ulp;
    double rms_rel_error;
    double max_rel_error;
    double avg_rel_error;
    double rms_abs_error;
    double max_abs_error;
    double avg_abs_error;
    size_t worst_input_idx;
    uint32_t worst_input_bits;
    uint32_t num_samples;

    double get_metric(OptimizationMetric metric) const;
};

// Comparison between two coefficient sets for a segment
struct SegmentComparison {
    uint32_t segment_idx;
    LUTEntry original_coeffs;
    LUTEntry optimized_coeffs;
    SegmentMetrics original_metrics;
    SegmentMetrics optimized_metrics;
    bool improved;
    double rms_ulp_delta;
    int64_t max_ulp_delta;
};

// Overall metrics across all segments
struct OverallMetrics {
    double total_rms_ulp;
    uint64_t global_max_ulp;
    double global_avg_ulp;
    double total_rms_rel_error;
    double global_max_rel_error;
    double global_avg_rel_error;
    double total_rms_abs_error;
    double global_max_abs_error;
    double global_avg_abs_error;
    uint32_t worst_segment_idx;
    uint64_t total_samples;

    double get_metric(OptimizationMetric metric) const {
        switch (metric) {
            case OptimizationMetric::RMS_ULP: return total_rms_ulp;
            case OptimizationMetric::MAX_ULP: return static_cast<double>(global_max_ulp);
            case OptimizationMetric::RMS_REL_ERROR: return total_rms_rel_error;
            case OptimizationMetric::MAX_REL_ERROR: return global_max_rel_error;
            case OptimizationMetric::RMS_ABS_ERROR: return total_rms_abs_error;
            case OptimizationMetric::MAX_ABS_ERROR: return global_max_abs_error;
            case OptimizationMetric::HYBRID_ULP_MAX_AVG:
                return 0.3 * static_cast<double>(global_max_ulp) + 0.7 * global_avg_ulp;
            case OptimizationMetric::HYBRID_ABS_MAX_AVG:
                return 0.3 * global_max_abs_error + 0.7 * global_avg_abs_error;
            case OptimizationMetric::HYBRID_REL_MAX_AVG:
                return 0.3 * global_max_rel_error + 0.7 * global_avg_rel_error;
            default: return total_rms_ulp;
        }
    }
};

// Optimization result for a function
struct OptimizationResult {
    SFUOp op;
    std::vector<LUTEntry> optimized_coeffs;
    std::vector<SegmentComparison> per_segment_report;
    OverallMetrics before;
    OverallMetrics after;
    int num_iterations;
    int num_improved_segments;
    double optimization_time_seconds;
};

// Function configuration for optimization
struct FunctionConfig {
    SFUOp op;
    std::string name;
    std::string lut_filename;
    std::string cache_filename;
    int num_segments;
    int m_bits;
    float range_start;
    float range_end;
    bool is_sqrt_rsqrt_even;     // For sqrt/rsqrt even table
    bool is_sqrt_rsqrt_odd;      // For sqrt/rsqrt odd table
};

// Helper function to get function configuration
inline std::vector<FunctionConfig> get_all_function_configs() {
    return {
        {SFUOp::EXP2, "exp2", "exp2-coeffs.txt", "exp2_1.0_2.0.bin", 64, 6, 1.0f, 2.0f, false, false},
        {SFUOp::LOG2, "log2", "log2-coeffs.txt", "log2_1.0_2.0.bin", 64, 6, 1.0f, 2.0f, false, false},
        {SFUOp::RCP, "rcp", "rcp-coeffs.txt", "rcp_1.0_2.0.bin", 128, 7, 1.0f, 2.0f, false, false},
        {SFUOp::SQRT, "sqrt-even", "sqrt-even-coeffs.txt", "sqrt-even_1.0_2.0.bin", 64, 6, 1.0f, 2.0f, true, false},
        {SFUOp::SQRT, "sqrt-odd", "sqrt-odd-coeffs.txt", "sqrt-odd_2.0_4.0.bin", 64, 6, 2.0f, 4.0f, false, true},
        {SFUOp::RSQRT, "rsqrt-even", "rsqrt-even-coeffs.txt", "rsqrt-even_1.0_2.0.bin", 64, 6, 1.0f, 2.0f, true, false},
        {SFUOp::RSQRT, "rsqrt-odd", "rsqrt-odd-coeffs.txt", "rsqrt-odd_2.0_4.0.bin", 64, 6, 2.0f, 4.0f, false, true},
        {SFUOp::SIN, "sin", "sin-coeffs.txt", "sin_1.0_2.0.bin", 64, 6, 1.0f, 2.0f, false, false},
        {SFUOp::TANH, "tanh", "tanh-coeffs.txt", "tanh_0.0_8.0.bin", 128, 7, 0.0f, 8.0f, false, false}
    };
}

inline FunctionConfig get_function_config(SFUOp op, bool even_table = true) {
    auto configs = get_all_function_configs();
    for (const auto& config : configs) {
        if (config.op == op) {
            if (op != SFUOp::SQRT && op != SFUOp::RSQRT) {
                return config;
            }
            // For SQRT/RSQRT, match the table type
            if (even_table && config.is_sqrt_rsqrt_even) {
                return config;
            }
            if (!even_table && config.is_sqrt_rsqrt_odd) {
                return config;
            }
        }
    }
    // Default fallback
    return configs[0];
}

inline double SegmentMetrics::get_metric(OptimizationMetric metric) const {
    switch (metric) {
        case OptimizationMetric::RMS_ULP: return rms_ulp;
        case OptimizationMetric::MAX_ULP: return static_cast<double>(max_ulp);
        case OptimizationMetric::RMS_REL_ERROR: return rms_rel_error;
        case OptimizationMetric::MAX_REL_ERROR: return max_rel_error;
        case OptimizationMetric::RMS_ABS_ERROR: return rms_abs_error;
        case OptimizationMetric::MAX_ABS_ERROR: return max_abs_error;
        case OptimizationMetric::HYBRID_ULP_MAX_AVG:
            return 0.3 * static_cast<double>(max_ulp) + 0.7 * avg_ulp;
        case OptimizationMetric::HYBRID_ABS_MAX_AVG:
            return 0.3 * max_abs_error + 0.7 * avg_abs_error;
        case OptimizationMetric::HYBRID_REL_MAX_AVG:
            return 0.3 * max_rel_error + 0.7 * avg_rel_error;
        default: return rms_ulp;
    }
}

inline CoeffOptimizationConfig get_default_optimization_config(SFUOp op) {
    CoeffOptimizationConfig config;
    config.optimize_c0 = false;
    config.optimize_c1 = false;
    config.optimize_c2 = true;
    config.c0_search_range = 1;
    config.c1_search_range = 1;
    config.c2_search_range = 1;

    switch (op) {
        case SFUOp::EXP2:
        case SFUOp::RCP:
        case SFUOp::SQRT:
        case SFUOp::RSQRT:
            config.primary_metric = OptimizationMetric::HYBRID_ULP_MAX_AVG;
            config.secondary_metric = OptimizationMetric::MAX_ULP;
            break;
        case SFUOp::LOG2:
        case SFUOp::SIN:
        case SFUOp::COS:
        case SFUOp::SIGMOID:
        case SFUOp::TANH:
            config.primary_metric = OptimizationMetric::HYBRID_ABS_MAX_AVG;
            config.secondary_metric = OptimizationMetric::MAX_ABS_ERROR;
            break;
        default:
            config.primary_metric = OptimizationMetric::HYBRID_ULP_MAX_AVG;
            config.secondary_metric = OptimizationMetric::MAX_ULP;
            break;
    }

    return config;
}

#endif // OPTIMIZER_TYPES_H
