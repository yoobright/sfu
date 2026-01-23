#include "coefficient_optimizer.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <omp.h>

CoefficientOptimizer::CoefficientOptimizer()
    : max_iterations_(10), verbose_(true) {
}

CoefficientOptimizer::~CoefficientOptimizer() {
}

LUTEntry CoefficientOptimizer::optimize_segment(
    uint32_t segment_idx,
    const LUTEntry& current,
    const GroundTruthCache& gt_cache,
    SFUOp op,
    bool is_sqrt_rsqrt_even,
    const CoeffOptimizationConfig& config,
    SegmentMetrics* out_metrics
) {
    std::vector<std::pair<LUTEntry, SegmentMetrics>> candidates;

    SegmentMetrics metrics_current = evaluator_.evaluate_segment(
        segment_idx, current, gt_cache, op, is_sqrt_rsqrt_even
    );
    candidates.push_back({current, metrics_current});

    int c0_range = config.optimize_c0 ? config.c0_search_range : 0;
    int c1_range = config.optimize_c1 ? config.c1_search_range : 0;
    int c2_range = config.optimize_c2 ? config.c2_search_range : 0;

    for (int dc0 = -c0_range; dc0 <= c0_range; dc0++) {
        for (int dc1 = -c1_range; dc1 <= c1_range; dc1++) {
            for (int dc2 = -c2_range; dc2 <= c2_range; dc2++) {
                if (dc0 == 0 && dc1 == 0 && dc2 == 0) continue;

                LUTEntry test = current;
                bool valid = true;

                if (config.optimize_c0) {
                    int64_t new_c0 = static_cast<int64_t>(current.c0) + dc0;
                    if (new_c0 < 0 || new_c0 > ((1ULL << 26) - 1)) {
                        valid = false;
                    } else {
                        test.c0 = static_cast<uint32_t>(new_c0);
                    }
                }

                if (valid && config.optimize_c1) {
                    int64_t new_c1 = static_cast<int64_t>(current.c1) + dc1;
                    if (new_c1 < 0 || new_c1 > ((1ULL << 16) - 1)) {
                        valid = false;
                    } else {
                        test.c1 = static_cast<uint32_t>(new_c1);
                    }
                }

                if (valid && config.optimize_c2) {
                    int64_t new_c2 = static_cast<int64_t>(current.c2) + dc2;
                    if (new_c2 < 0 || new_c2 > ((1ULL << 12) - 1)) {
                        valid = false;
                    } else {
                        test.c2 = static_cast<uint32_t>(new_c2);
                    }
                }

                if (!valid) continue;

                SegmentMetrics metrics = evaluator_.evaluate_segment(
                    segment_idx, test, gt_cache, op, is_sqrt_rsqrt_even
                );
                candidates.push_back({test, metrics});
            }
        }
    }

    auto best = std::min_element(candidates.begin(), candidates.end(),
        [&config](const auto& a, const auto& b) {
            double metric_a = a.second.get_metric(config.primary_metric);
            double metric_b = b.second.get_metric(config.primary_metric);
            if (std::abs(metric_a - metric_b) > 1e-9) {
                return metric_a < metric_b;
            }
            double secondary_a = a.second.get_metric(config.secondary_metric);
            double secondary_b = b.second.get_metric(config.secondary_metric);
            return secondary_a < secondary_b;
        }
    );

    if (out_metrics) {
        *out_metrics = best->second;
    }

    return best->first;
}

int CoefficientOptimizer::optimize_iteration(
    std::vector<LUTEntry>& coeffs,
    const GroundTruthCache& gt_cache,
    SFUOp op,
    bool is_sqrt_rsqrt_even,
    const CoeffOptimizationConfig& config,
    int iteration_num
) {
    uint32_t num_segments = static_cast<uint32_t>(coeffs.size());
    int num_improved = 0;

    OverallMetrics metrics_before;
    if (verbose_) {
        metrics_before = evaluator_.evaluate_all_segments(
            coeffs, gt_cache, op, is_sqrt_rsqrt_even
        );
    }

    std::vector<LUTEntry> new_coeffs = coeffs;

    #pragma omp parallel for schedule(dynamic) reduction(+:num_improved)
    for (uint32_t seg = 0; seg < num_segments; seg++) {
        SegmentMetrics metrics;
        LUTEntry optimized = optimize_segment(
            seg, coeffs[seg], gt_cache, op, is_sqrt_rsqrt_even, config, &metrics
        );

        if (optimized.c0 != coeffs[seg].c0 ||
            optimized.c1 != coeffs[seg].c1 ||
            optimized.c2 != coeffs[seg].c2) {
            new_coeffs[seg] = optimized;
            num_improved++;
        }
    }

    coeffs = new_coeffs;

    if (verbose_) {
        OverallMetrics metrics_after = evaluator_.evaluate_all_segments(
            coeffs, gt_cache, op, is_sqrt_rsqrt_even
        );

        printf("[Iteration %d] ", iteration_num);
        for (uint32_t i = 0; i < 20; i++) {
            printf("━");
        }
        printf(" %u/%u segments\n", num_segments, num_segments);

        printf("  Improved: %d/%u (%.1f%%)\n",
               num_improved, num_segments, 100.0 * num_improved / num_segments);

        const char* metric_name = "Unknown";
        switch (config.primary_metric) {
            case OptimizationMetric::RMS_ULP: metric_name = "RMS ULP"; break;
            case OptimizationMetric::MAX_ULP: metric_name = "Max ULP"; break;
            case OptimizationMetric::RMS_REL_ERROR: metric_name = "RMS Rel Err"; break;
            case OptimizationMetric::MAX_REL_ERROR: metric_name = "Max Rel Err"; break;
            case OptimizationMetric::RMS_ABS_ERROR: metric_name = "RMS Abs Err"; break;
            case OptimizationMetric::MAX_ABS_ERROR: metric_name = "Max Abs Err"; break;
            case OptimizationMetric::HYBRID_ULP_MAX_AVG: metric_name = "Hybrid ULP"; break;
            case OptimizationMetric::HYBRID_ABS_MAX_AVG: metric_name = "Hybrid Abs"; break;
            case OptimizationMetric::HYBRID_REL_MAX_AVG: metric_name = "Hybrid Rel"; break;
        }

        double metric_before = metrics_before.get_metric(config.primary_metric);
        double metric_after = metrics_after.get_metric(config.primary_metric);
        double change = ((metric_after - metric_before) / metric_before) * 100.0;
        printf("  %s: %.6f → %.6f (%.2f%% %s)\n",
               metric_name, metric_before, metric_after, std::abs(change),
               change < 0 ? "↓" : (change > 0 ? "↑" : "-"));

        printf("  Max ULP: %3lu → %3lu    Avg ULP: %10.6f → %10.6f\n",
               metrics_before.global_max_ulp, metrics_after.global_max_ulp,
               metrics_before.global_avg_ulp, metrics_after.global_avg_ulp);

        printf("  Max Rel: %10.3e → %10.3e    Avg Rel: %10.3e → %10.3e\n",
               metrics_before.global_max_rel_error, metrics_after.global_max_rel_error,
               metrics_before.global_avg_rel_error, metrics_after.global_avg_rel_error);

        printf("  Max Abs: %10.3e → %10.3e    Avg Abs: %10.3e → %10.3e\n",
               metrics_before.global_max_abs_error, metrics_after.global_max_abs_error,
               metrics_before.global_avg_abs_error, metrics_after.global_avg_abs_error);

        printf("\n");
    }

    return num_improved;
}

OptimizationResult CoefficientOptimizer::optimize_function(
    SFUOp op,
    const std::string& input_lut_path,
    const std::string& cache_path,
    const std::string& output_lut_path,
    bool is_sqrt_rsqrt_even,
    const CoeffOptimizationConfig* config
) {
    OptimizationResult result;
    result.op = op;
    result.num_iterations = 0;
    result.num_improved_segments = 0;

    CoeffOptimizationConfig opt_config = config ? *config : get_default_optimization_config(op);

    double start_time = omp_get_wtime();

    if (verbose_) {
        printf("[Optimizer] Loading coefficients from %s\n", input_lut_path.c_str());
    }
    std::vector<LUTEntry> coeffs = CoefficientIO::load(input_lut_path);

    if (verbose_) {
        printf("[Optimizer] Loading ground truth cache from %s\n", cache_path.c_str());
    }
    GroundTruthCache gt_cache;
    gt_cache.load(cache_path);

    if (verbose_) {
        printf("[Optimizer] Optimization config:\n");
        printf("  C0: %s", opt_config.optimize_c0 ? "ON" : "OFF");
        if (opt_config.optimize_c0) printf(" (range: ±%d)", opt_config.c0_search_range);
        printf(", C1: %s", opt_config.optimize_c1 ? "ON" : "OFF");
        if (opt_config.optimize_c1) printf(" (range: ±%d)", opt_config.c1_search_range);
        printf(", C2: %s", opt_config.optimize_c2 ? "ON" : "OFF");
        if (opt_config.optimize_c2) printf(" (range: ±%d)", opt_config.c2_search_range);
        printf("\n");

        int total_candidates = 1;
        if (opt_config.optimize_c0) total_candidates *= (2 * opt_config.c0_search_range + 1);
        if (opt_config.optimize_c1) total_candidates *= (2 * opt_config.c1_search_range + 1);
        if (opt_config.optimize_c2) total_candidates *= (2 * opt_config.c2_search_range + 1);
        printf("  Candidates per segment: %d\n", total_candidates);

        const char* primary_name = "Unknown";
        switch (opt_config.primary_metric) {
            case OptimizationMetric::RMS_ULP: primary_name = "RMS ULP"; break;
            case OptimizationMetric::MAX_ULP: primary_name = "Max ULP"; break;
            case OptimizationMetric::RMS_REL_ERROR: primary_name = "RMS Rel Error"; break;
            case OptimizationMetric::MAX_REL_ERROR: primary_name = "Max Rel Error"; break;
            case OptimizationMetric::RMS_ABS_ERROR: primary_name = "RMS Abs Error"; break;
            case OptimizationMetric::MAX_ABS_ERROR: primary_name = "Max Abs Error"; break;
            case OptimizationMetric::HYBRID_ULP_MAX_AVG: primary_name = "Hybrid ULP (0.3*max + 0.7*avg)"; break;
            case OptimizationMetric::HYBRID_ABS_MAX_AVG: primary_name = "Hybrid Abs (0.3*max + 0.7*avg)"; break;
            case OptimizationMetric::HYBRID_REL_MAX_AVG: primary_name = "Hybrid Rel (0.3*max + 0.7*avg)"; break;
            default:
                printf("  DEBUG: Unknown metric value = %d\n", static_cast<int>(opt_config.primary_metric));
                break;
        }
        printf("  Primary metric: %s\n", primary_name);
        printf("\n");
    }

    if (verbose_) {
        printf("[Optimizer] Evaluating initial state...\n");
    }
    result.before = evaluator_.evaluate_all_segments(coeffs, gt_cache, op, is_sqrt_rsqrt_even);

    if (verbose_) {
        printf("[Optimizer] Initial metrics:\n");
        printf("  Max ULP: %3lu    Avg ULP: %10.6f    RMS ULP: %10.6f\n",
               result.before.global_max_ulp, result.before.global_avg_ulp, result.before.total_rms_ulp);
        printf("  Max Rel: %10.3e    Avg Rel: %10.3e    RMS Rel: %10.3e\n",
               result.before.global_max_rel_error, result.before.global_avg_rel_error, result.before.total_rms_rel_error);
        printf("  Max Abs: %10.3e    Avg Abs: %10.3e    RMS Abs: %10.3e\n",
               result.before.global_max_abs_error, result.before.global_avg_abs_error, result.before.total_rms_abs_error);
        printf("\n");
    }

    bool any_improved = true;
    int iteration = 0;

    while (any_improved && iteration < max_iterations_) {
        iteration++;
        result.num_iterations = iteration;

        int num_improved = optimize_iteration(
            coeffs, gt_cache, op, is_sqrt_rsqrt_even, opt_config, iteration
        );

        result.num_improved_segments += num_improved;
        any_improved = (num_improved > 0);
    }

    result.after = evaluator_.evaluate_all_segments(coeffs, gt_cache, op, is_sqrt_rsqrt_even);
    result.optimized_coeffs = coeffs;

    double elapsed = omp_get_wtime() - start_time;
    result.optimization_time_seconds = elapsed;

    if (verbose_) {
        printf("[Optimizer] Converged after %d iterations\n", result.num_iterations);
        printf("[Optimizer] Final metrics:\n");
        printf("  Max ULP: %3lu    Avg ULP: %10.6f    RMS ULP: %10.6f\n",
               result.after.global_max_ulp, result.after.global_avg_ulp, result.after.total_rms_ulp);
        printf("  Max Rel: %10.3e    Avg Rel: %10.3e    RMS Rel: %10.3e\n",
               result.after.global_max_rel_error, result.after.global_avg_rel_error, result.after.total_rms_rel_error);
        printf("  Max Abs: %10.3e    Avg Abs: %10.3e    RMS Abs: %10.3e\n",
               result.after.global_max_abs_error, result.after.global_avg_abs_error, result.after.total_rms_abs_error);
        printf("[Optimizer] Total time: %.2f seconds\n\n", elapsed);
    }

    if (verbose_) {
        printf("[Optimizer] Saving optimized coefficients to %s\n", output_lut_path.c_str());
    }

    if (CoefficientIO::exists(output_lut_path)) {
        CoefficientIO::backup(output_lut_path);
    }

    CoefficientIO::save(output_lut_path, result.optimized_coeffs);

    return result;
}
