#include "coefficient_optimizer.h"
#include "ground_truth_cache.h"
#include "coefficient_io.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

void print_usage() {
    printf("Usage:\n");
    printf("  optimizer cache --all --output <dir>\n");
    printf("  optimizer cache --op <function> --output <path>\n\n");
    printf("  optimizer optimize --op <function> --input <path> --output <path> --cache <path>\n");
    printf("    [--c0] [--c1] [--c2] [--metric <metric>]\n");
    printf("    [--c0-range <n>] [--c1-range <n>] [--c2-range <n>]\n");
    printf("    Options:\n");
    printf("      --c0, --c1, --c2: Enable optimization for specific coefficients\n");
    printf("      --c0-range, --c1-range, --c2-range: Search range (default: 1)\n");
    printf("      --metric: ulp|rel|abs|hybrid (default: auto hybrid per function)\n\n");
    printf("  optimizer optimize-all --input-dir <dir> --output-dir <dir> --cache-dir <dir>\n");
    printf("    [--c0] [--c1] [--c2] [--c0-range <n>] [--c1-range <n>] [--c2-range <n>]\n\n");
    printf("  optimizer validate --op <function> --original <path> --optimized <path> --cache <path>\n\n");
    printf("Functions: exp2, log2, rcp, sqrt-even, sqrt-odd, rsqrt-even, rsqrt-odd, sin\n");
}

void ensure_directory_exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

int cmd_cache_all(const std::string& output_dir) {
    ensure_directory_exists(output_dir);

    auto configs = get_all_function_configs();

    printf("[Cache] Generating caches for all functions...\n\n");

    for (const auto& config : configs) {
        std::string cache_path = output_dir + "/" + config.cache_filename;

        try {
            GroundTruthCache::generate(
                config.op,
                config.range_start,
                config.range_end,
                config.m_bits,
                cache_path
            );
            printf("\n");
        } catch (const std::exception& e) {
            fprintf(stderr, "Error generating cache for %s: %s\n",
                    config.name.c_str(), e.what());
            return 1;
        }
    }

    printf("[Cache] All caches generated successfully\n");
    return 0;
}

int cmd_cache_single(const std::string& function_name, const std::string& output_path) {
    auto configs = get_all_function_configs();

    for (const auto& config : configs) {
        if (config.name == function_name) {
            try {
                GroundTruthCache::generate(
                    config.op,
                    config.range_start,
                    config.range_end,
                    config.m_bits,
                    output_path
                );
                printf("[Cache] Successfully generated cache\n");
                return 0;
            } catch (const std::exception& e) {
                fprintf(stderr, "Error: %s\n", e.what());
                return 1;
            }
        }
    }

    fprintf(stderr, "Unknown function: %s\n", function_name.c_str());
    return 1;
}

int cmd_optimize(const std::string& function_name,
                 const std::string& input_path,
                 const std::string& output_path,
                 const std::string& cache_path,
                 const CoeffOptimizationConfig* opt_config) {
    auto configs = get_all_function_configs();

    for (const auto& config : configs) {
        if (config.name == function_name) {
            try {
                CoefficientOptimizer optimizer;
                optimizer.set_verbose(true);

                bool is_even = config.is_sqrt_rsqrt_even || (!config.is_sqrt_rsqrt_odd);

                OptimizationResult result = optimizer.optimize_function(
                    config.op,
                    input_path,
                    cache_path,
                    output_path,
                    is_even,
                    opt_config
                );

                printf("[Success] Optimization complete\n");
                printf("  Iterations: %d\n", result.num_iterations);
                printf("  Improved segments: %d/%zu\n",
                       result.num_improved_segments,
                       result.optimized_coeffs.size());
                printf("  RMS ULP: %.6f → %.6f\n",
                       result.before.total_rms_ulp,
                       result.after.total_rms_ulp);
                printf("  Time: %.2f seconds\n", result.optimization_time_seconds);

                return 0;
            } catch (const std::exception& e) {
                fprintf(stderr, "Error: %s\n", e.what());
                return 1;
            }
        }
    }

    fprintf(stderr, "Unknown function: %s\n", function_name.c_str());
    return 1;
}

int cmd_optimize_all(const std::string& input_dir,
                     const std::string& output_dir,
                     const std::string& cache_dir,
                     const CoeffOptimizationConfig* base_config) {
    ensure_directory_exists(output_dir);

    auto configs = get_all_function_configs();

    printf("[Optimizer] Optimizing all functions...\n\n");

    int total_improved = 0;
    double total_time = 0.0;

    for (const auto& config : configs) {
        std::string input_path = input_dir + "/" + config.lut_filename;
        std::string output_path = output_dir + "/" + config.lut_filename;
        std::string cache_path = cache_dir + "/" + config.cache_filename;

        printf("========================================\n");
        printf("Optimizing: %s\n", config.name.c_str());
        printf("========================================\n");

        try {
            CoefficientOptimizer optimizer;
            optimizer.set_verbose(true);

            bool is_even = config.is_sqrt_rsqrt_even || (!config.is_sqrt_rsqrt_odd);

            CoeffOptimizationConfig func_config;
            const CoeffOptimizationConfig* config_ptr = nullptr;

            if (base_config) {
                func_config = get_default_optimization_config(config.op);
                func_config.optimize_c0 = base_config->optimize_c0;
                func_config.optimize_c1 = base_config->optimize_c1;
                func_config.optimize_c2 = base_config->optimize_c2;
                func_config.c0_search_range = base_config->c0_search_range;
                func_config.c1_search_range = base_config->c1_search_range;
                func_config.c2_search_range = base_config->c2_search_range;
                config_ptr = &func_config;
            }

            OptimizationResult result = optimizer.optimize_function(
                config.op,
                input_path,
                cache_path,
                output_path,
                is_even,
                config_ptr
            );

            total_improved += result.num_improved_segments;
            total_time += result.optimization_time_seconds;

        } catch (const std::exception& e) {
            fprintf(stderr, "Error optimizing %s: %s\n",
                    config.name.c_str(), e.what());
            return 1;
        }
    }

    printf("========================================\n");
    printf("[Success] All functions optimized\n");
    printf("  Total improved segments: %d\n", total_improved);
    printf("  Total time: %.2f seconds\n", total_time);
    printf("========================================\n");

    return 0;
}

int cmd_validate(const std::string& function_name,
                 const std::string& original_path,
                 const std::string& optimized_path,
                 const std::string& cache_path) {
    auto configs = get_all_function_configs();

    for (const auto& config : configs) {
        if (config.name == function_name) {
            try {
                auto original_coeffs = CoefficientIO::load(original_path);
                auto optimized_coeffs = CoefficientIO::load(optimized_path);

                GroundTruthCache gt_cache;
                gt_cache.load(cache_path);

                SegmentEvaluator evaluator;
                bool is_even = config.is_sqrt_rsqrt_even || (!config.is_sqrt_rsqrt_odd);

                printf("[Validate] Evaluating original coefficients...\n");
                OverallMetrics original_metrics = evaluator.evaluate_all_segments(
                    original_coeffs, gt_cache, config.op, is_even
                );

                printf("[Validate] Evaluating optimized coefficients...\n");
                OverallMetrics optimized_metrics = evaluator.evaluate_all_segments(
                    optimized_coeffs, gt_cache, config.op, is_even
                );

                printf("\n========================================\n");
                printf("Validation Results: %s\n", config.name.c_str());
                printf("========================================\n");
                printf("RMS ULP:     %.6f → %.6f (%.2f%% change)\n",
                       original_metrics.total_rms_ulp,
                       optimized_metrics.total_rms_ulp,
                       ((optimized_metrics.total_rms_ulp - original_metrics.total_rms_ulp) /
                        original_metrics.total_rms_ulp) * 100.0);
                printf("Max ULP:     %lu → %lu\n",
                       original_metrics.global_max_ulp,
                       optimized_metrics.global_max_ulp);
                printf("Avg ULP:     %.6f → %.6f\n",
                       original_metrics.global_avg_ulp,
                       optimized_metrics.global_avg_ulp);
                printf("Max Rel Err: %.6e → %.6e\n",
                       original_metrics.global_max_rel_error,
                       optimized_metrics.global_max_rel_error);
                printf("========================================\n");

                return 0;
            } catch (const std::exception& e) {
                fprintf(stderr, "Error: %s\n", e.what());
                return 1;
            }
        }
    }

    fprintf(stderr, "Unknown function: %s\n", function_name.c_str());
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "cache") {
        bool all = false;
        std::string op_name;
        std::string output;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--all") == 0) {
                all = true;
            } else if (strcmp(argv[i], "--op") == 0 && i + 1 < argc) {
                op_name = argv[++i];
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output = argv[++i];
            }
        }

        if (output.empty()) {
            fprintf(stderr, "Error: --output is required\n");
            return 1;
        }

        if (all) {
            return cmd_cache_all(output);
        } else if (!op_name.empty()) {
            return cmd_cache_single(op_name, output);
        } else {
            fprintf(stderr, "Error: specify --all or --op <function>\n");
            return 1;
        }
    }
    else if (command == "optimize") {
        std::string op_name, input, output, cache;
        CoeffOptimizationConfig opt_config;
        bool has_coeff_flags = false;
        opt_config.optimize_c0 = false;
        opt_config.optimize_c1 = false;
        opt_config.optimize_c2 = false;
        opt_config.c0_search_range = 1;
        opt_config.c1_search_range = 1;
        opt_config.c2_search_range = 1;
        std::string metric_str;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--op") == 0 && i + 1 < argc) {
                op_name = argv[++i];
            } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
                input = argv[++i];
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output = argv[++i];
            } else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
                cache = argv[++i];
            } else if (strcmp(argv[i], "--c0") == 0) {
                opt_config.optimize_c0 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c1") == 0) {
                opt_config.optimize_c1 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c2") == 0) {
                opt_config.optimize_c2 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c0-range") == 0 && i + 1 < argc) {
                opt_config.c0_search_range = std::atoi(argv[++i]);
            } else if (strcmp(argv[i], "--c1-range") == 0 && i + 1 < argc) {
                opt_config.c1_search_range = std::atoi(argv[++i]);
            } else if (strcmp(argv[i], "--c2-range") == 0 && i + 1 < argc) {
                opt_config.c2_search_range = std::atoi(argv[++i]);
            } else if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) {
                metric_str = argv[++i];
            }
        }

        if (op_name.empty() || input.empty() || output.empty() || cache.empty()) {
            fprintf(stderr, "Error: --op, --input, --output, and --cache are required\n");
            return 1;
        }

        auto configs = get_all_function_configs();
        SFUOp op = SFUOp::EXP2;
        bool found = false;
        for (const auto& cfg : configs) {
            if (cfg.name == op_name) {
                op = cfg.op;
                found = true;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "Unknown function: %s\n", op_name.c_str());
            return 1;
        }

        const CoeffOptimizationConfig* config_ptr = nullptr;
        if (has_coeff_flags || !metric_str.empty()) {
            if (!has_coeff_flags) {
                opt_config.optimize_c2 = true;
            }

            auto default_config = get_default_optimization_config(op);

            if (!metric_str.empty()) {
                if (metric_str == "ulp") {
                    opt_config.primary_metric = OptimizationMetric::RMS_ULP;
                    opt_config.secondary_metric = OptimizationMetric::MAX_ULP;
                } else if (metric_str == "rel") {
                    opt_config.primary_metric = OptimizationMetric::RMS_REL_ERROR;
                    opt_config.secondary_metric = OptimizationMetric::MAX_REL_ERROR;
                } else if (metric_str == "abs") {
                    opt_config.primary_metric = OptimizationMetric::RMS_ABS_ERROR;
                    opt_config.secondary_metric = OptimizationMetric::MAX_ABS_ERROR;
                } else if (metric_str == "hybrid") {
                    opt_config.primary_metric = default_config.primary_metric;
                    opt_config.secondary_metric = default_config.secondary_metric;
                } else {
                    fprintf(stderr, "Unknown metric: %s (use ulp|rel|abs|hybrid)\n", metric_str.c_str());
                    return 1;
                }
            } else {
                opt_config.primary_metric = default_config.primary_metric;
                opt_config.secondary_metric = default_config.secondary_metric;
            }

            config_ptr = &opt_config;
        }

        return cmd_optimize(op_name, input, output, cache, config_ptr);
    }
    else if (command == "optimize-all") {
        std::string input_dir, output_dir, cache_dir;
        CoeffOptimizationConfig opt_config;
        bool has_coeff_flags = false;
        opt_config.optimize_c0 = false;
        opt_config.optimize_c1 = false;
        opt_config.optimize_c2 = false;
        opt_config.c0_search_range = 1;
        opt_config.c1_search_range = 1;
        opt_config.c2_search_range = 1;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--input-dir") == 0 && i + 1 < argc) {
                input_dir = argv[++i];
            } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
                cache_dir = argv[++i];
            } else if (strcmp(argv[i], "--c0") == 0) {
                opt_config.optimize_c0 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c1") == 0) {
                opt_config.optimize_c1 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c2") == 0) {
                opt_config.optimize_c2 = true;
                has_coeff_flags = true;
            } else if (strcmp(argv[i], "--c0-range") == 0 && i + 1 < argc) {
                opt_config.c0_search_range = std::atoi(argv[++i]);
            } else if (strcmp(argv[i], "--c1-range") == 0 && i + 1 < argc) {
                opt_config.c1_search_range = std::atoi(argv[++i]);
            } else if (strcmp(argv[i], "--c2-range") == 0 && i + 1 < argc) {
                opt_config.c2_search_range = std::atoi(argv[++i]);
            }
        }

        if (input_dir.empty() || output_dir.empty() || cache_dir.empty()) {
            fprintf(stderr, "Error: --input-dir, --output-dir, and --cache-dir are required\n");
            return 1;
        }

        const CoeffOptimizationConfig* config_ptr = nullptr;
        if (has_coeff_flags) {
            config_ptr = &opt_config;
        }

        return cmd_optimize_all(input_dir, output_dir, cache_dir, config_ptr);
    }
    else if (command == "validate") {
        std::string op_name, original, optimized, cache;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--op") == 0 && i + 1 < argc) {
                op_name = argv[++i];
            } else if (strcmp(argv[i], "--original") == 0 && i + 1 < argc) {
                original = argv[++i];
            } else if (strcmp(argv[i], "--optimized") == 0 && i + 1 < argc) {
                optimized = argv[++i];
            } else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
                cache = argv[++i];
            }
        }

        if (op_name.empty() || original.empty() || optimized.empty() || cache.empty()) {
            fprintf(stderr, "Error: --op, --original, --optimized, and --cache are required\n");
            return 1;
        }

        return cmd_validate(op_name, original, optimized, cache);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command.c_str());
        print_usage();
        return 1;
    }
}
