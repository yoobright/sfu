#ifndef COEFFICIENT_OPTIMIZER_H
#define COEFFICIENT_OPTIMIZER_H

#include "optimizer_types.h"
#include "ground_truth_cache.h"
#include "segment_evaluator.h"
#include "coefficient_io.h"
#include <string>

class CoefficientOptimizer {
public:
    CoefficientOptimizer();
    ~CoefficientOptimizer();

    OptimizationResult optimize_function(
        SFUOp op,
        const std::string& input_lut_path,
        const std::string& cache_path,
        const std::string& output_lut_path,
        bool is_sqrt_rsqrt_even = true,
        const CoeffOptimizationConfig* config = nullptr
    );

    void set_max_iterations(int max_iter) { max_iterations_ = max_iter; }
    void set_verbose(bool verbose) { verbose_ = verbose; }
    void set_optimization_config(const CoeffOptimizationConfig& config) {
        optimization_config_ = config;
    }

private:
    LUTEntry optimize_segment(
        uint32_t segment_idx,
        const LUTEntry& current,
        const GroundTruthCache& gt_cache,
        SFUOp op,
        bool is_sqrt_rsqrt_even,
        const CoeffOptimizationConfig& config,
        SegmentMetrics* out_metrics
    );

    int optimize_iteration(
        std::vector<LUTEntry>& coeffs,
        const GroundTruthCache& gt_cache,
        SFUOp op,
        bool is_sqrt_rsqrt_even,
        const CoeffOptimizationConfig& config,
        int iteration_num
    );

    SegmentEvaluator evaluator_;
    int max_iterations_;
    bool verbose_;
    CoeffOptimizationConfig optimization_config_;
};

#endif
