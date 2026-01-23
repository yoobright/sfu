#ifndef GROUND_TRUTH_CACHE_H
#define GROUND_TRUTH_CACHE_H

#include <string>
#include <vector>
#include <cstdint>
#include "optimizer_types.h"

class GroundTruthCache {
public:
    GroundTruthCache();
    ~GroundTruthCache();

    // Generate ground truth cache for a function
    static void generate(
        SFUOp op,
        float range_start,
        float range_end,
        int m_bits,
        const std::string& cache_path
    );

    // Load existing cache
    void load(const std::string& cache_path);

    // Get ground truth for a specific segment and local index
    float get_ground_truth(uint32_t segment_idx, uint32_t local_idx) const;

    // Get input value for a specific segment and local index
    float get_input(uint32_t segment_idx, uint32_t local_idx) const;

    // Get number of samples in a segment
    uint32_t get_segment_size(uint32_t segment_idx) const;

    // Get total number of segments
    uint32_t get_num_segments() const { return header_.num_segments; }

    // Get total number of samples
    uint32_t get_num_samples() const { return header_.num_samples; }

    // Get function op
    SFUOp get_op() const { return static_cast<SFUOp>(header_.function_op); }

private:
    GroundTruthHeader header_;
    std::vector<GroundTruthEntry> entries_;
    std::vector<uint32_t> segment_offsets_;  // Offset to first entry of each segment

    // Generate input values for a range
    static std::vector<uint32_t> generate_inputs(
        float range_start,
        float range_end,
        int m_bits
    );

    // Convert float to uint32_t bits
    static uint32_t float_to_bits(float f);

    // Convert uint32_t bits to float
    static float bits_to_float(uint32_t bits);
};

#endif // GROUND_TRUTH_CACHE_H
