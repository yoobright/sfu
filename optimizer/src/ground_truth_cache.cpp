#include "ground_truth_cache.h"
#include "mpfr_sfu.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <omp.h>
#include <stdexcept>

GroundTruthCache::GroundTruthCache() {
  std::memset(&header_, 0, sizeof(header_));
}

GroundTruthCache::~GroundTruthCache() {}

uint32_t GroundTruthCache::float_to_bits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(float));
  return bits;
}

float GroundTruthCache::bits_to_float(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(float));
  return f;
}

std::vector<uint32_t> GroundTruthCache::generate_inputs(SFUOp op,
                                                        float range_start,
                                                        float range_end,
                                                        int m_bits) {
  std::vector<uint32_t> inputs;

  if (op == SFUOp::SIGMOID) {
    // The hardware segmenter uses |x| / 8 in Q0.23. Exhaust every reduced
    // argument exactly once, matching 128 segments x 65536 local values.
    constexpr uint32_t kReducedValues = 1U << 23;
    inputs.reserve(kReducedValues);
    for (uint32_t reduced = 0; reduced < kReducedValues; ++reduced) {
      float input = static_cast<float>(reduced) * 0x1p-20f;
      inputs.push_back(float_to_bits(input));
    }
    return inputs;
  }

  // Get the exponent from range_start
  uint32_t start_bits = float_to_bits(range_start);
  uint32_t end_bits = float_to_bits(range_end);

  uint8_t start_exp = (start_bits >> 23) & 0xFF;
  uint8_t end_exp = (end_bits >> 23) & 0xFF;

  const uint32_t mantissa_bits = 23;
  const uint32_t num_segments = 1 << m_bits;
  const uint32_t mantissa_per_segment = 1 << (mantissa_bits - m_bits);

  // For range [1.0, 2.0], we have exp=127 (biased, meaning real exp=0)
  // For range [2.0, 4.0], we have exp=128 (biased, meaning real exp=1)

  if (start_exp == end_exp) {
    // Single exponent range (e.g., [1.0, 2.0])
    uint32_t base_exponent = start_exp;

    for (uint32_t seg = 0; seg < num_segments; seg++) {
      uint32_t mantissa_start = seg * mantissa_per_segment;
      for (uint32_t local = 0; local < mantissa_per_segment; local++) {
        uint32_t mantissa = mantissa_start + local;
        uint32_t float_bits = (base_exponent << 23) | mantissa;
        inputs.push_back(float_bits);
      }
    }
  } else {
    // Two exponent range (e.g., [2.0, 4.0])
    // Start at 2.0 (exp=128, mantissa=0) and go to 4.0 (exp=129, mantissa=0)
    // This means exp=128 with all mantissa values
    uint32_t base_exponent = start_exp;

    for (uint32_t seg = 0; seg < num_segments; seg++) {
      uint32_t mantissa_start = seg * mantissa_per_segment;
      for (uint32_t local = 0; local < mantissa_per_segment; local++) {
        uint32_t mantissa = mantissa_start + local;
        uint32_t float_bits = (base_exponent << 23) | mantissa;
        inputs.push_back(float_bits);
      }
    }
  }

  return inputs;
}

void GroundTruthCache::generate(SFUOp op, float range_start, float range_end,
                                int m_bits, const std::string &cache_path) {
  printf(
      "[Cache] Generating ground truth for op=%d, range=[%.1f, %.1f], m=%d\n",
      static_cast<int>(op), range_start, range_end, m_bits);

  // Generate all inputs
  auto input_bits = generate_inputs(op, range_start, range_end, m_bits);
  printf("[Cache] Generated %zu input values\n", input_bits.size());

  std::vector<float> inputs(input_bits.size());
  for (size_t i = 0; i < input_bits.size(); i++) {
    inputs[i] = bits_to_float(input_bits[i]);
  }

  std::vector<float> mpfr_inputs(inputs.size());
  if (op == SFUOp::SIN || op == SFUOp::COS) {
    const float PI_2 = static_cast<float>(M_PI) / 2.0f;
    for (size_t i = 0; i < inputs.size(); i++) {
      mpfr_inputs[i] = inputs[i] * PI_2;
    }
  } else {
    mpfr_inputs = inputs;
  }

  std::vector<float> outputs(inputs.size());

  printf("[Cache] Computing MPFR ground truth...\n");
  double start_time = omp_get_wtime();

  const size_t batch_size = 65536;
#pragma omp parallel for schedule(dynamic)
  for (size_t batch_start = 0; batch_start < inputs.size();
       batch_start += batch_size) {
    size_t batch_end = std::min(batch_start + batch_size, inputs.size());
    size_t batch_len = batch_end - batch_start;

    MPFR::compute_batch_float(&mpfr_inputs[batch_start], &outputs[batch_start],
                              batch_len, op);
  }

  double elapsed = omp_get_wtime() - start_time;
  printf("[Cache] MPFR computation completed in %.2f seconds\n", elapsed);

  // Build cache entries
  std::vector<GroundTruthEntry> entries(inputs.size());
  for (size_t i = 0; i < inputs.size(); i++) {
    entries[i].input_bits = input_bits[i];
    entries[i].output_bits = float_to_bits(outputs[i]);
  }

  // Build header
  GroundTruthHeader header;
  std::memset(&header, 0, sizeof(header));
  header.magic = 0x47544348; // "GTCH"
  header.function_op = static_cast<uint32_t>(op);
  header.num_samples = static_cast<uint32_t>(entries.size());
  header.num_segments = 1 << m_bits;
  header.m_bits = m_bits;
  header.range_start = range_start;
  header.range_end = range_end;

  // Write to file
  printf("[Cache] Writing to %s\n", cache_path.c_str());
  std::ofstream file(cache_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to create cache file: " + cache_path);
  }

  file.write(reinterpret_cast<const char *>(&header), sizeof(header));
  file.write(reinterpret_cast<const char *>(entries.data()),
             entries.size() * sizeof(GroundTruthEntry));

  if (!file.good()) {
    throw std::runtime_error("Error writing cache file: " + cache_path);
  }

  printf("[Cache] Successfully generated cache with %u samples\n",
         header.num_samples);
}

void GroundTruthCache::load(const std::string &cache_path) {
  std::ifstream file(cache_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open cache file: " + cache_path);
  }

  // Read header
  file.read(reinterpret_cast<char *>(&header_), sizeof(header_));
  if (!file.good()) {
    throw std::runtime_error("Failed to read cache header: " + cache_path);
  }

  // Verify magic
  if (header_.magic != 0x47544348) {
    throw std::runtime_error("Invalid cache file magic: " + cache_path);
  }

  // Read entries
  entries_.resize(header_.num_samples);
  file.read(reinterpret_cast<char *>(entries_.data()),
            entries_.size() * sizeof(GroundTruthEntry));
  if (!file.good()) {
    throw std::runtime_error("Failed to read cache entries: " + cache_path);
  }

  // Build segment offset table for O(1) access
  const uint32_t mantissa_bits = 23;
  const uint32_t mantissa_per_segment = 1 << (mantissa_bits - header_.m_bits);

  segment_offsets_.resize(header_.num_segments);
  for (uint32_t seg = 0; seg < header_.num_segments; seg++) {
    segment_offsets_[seg] = seg * mantissa_per_segment;
  }

  printf("[Cache] Loaded cache: %u segments, %u samples\n",
         header_.num_segments, header_.num_samples);
}

float GroundTruthCache::get_ground_truth(uint32_t segment_idx,
                                         uint32_t local_idx) const {
  if (segment_idx >= header_.num_segments) {
    throw std::out_of_range("Segment index out of range");
  }

  uint32_t global_idx = segment_offsets_[segment_idx] + local_idx;
  if (global_idx >= entries_.size()) {
    throw std::out_of_range("Global index out of range");
  }

  return bits_to_float(entries_[global_idx].output_bits);
}

float GroundTruthCache::get_input(uint32_t segment_idx,
                                  uint32_t local_idx) const {
  if (segment_idx >= header_.num_segments) {
    throw std::out_of_range("Segment index out of range");
  }

  uint32_t global_idx = segment_offsets_[segment_idx] + local_idx;
  if (global_idx >= entries_.size()) {
    throw std::out_of_range("Global index out of range");
  }

  return bits_to_float(entries_[global_idx].input_bits);
}

uint32_t GroundTruthCache::get_segment_size(uint32_t segment_idx) const {
  if (segment_idx >= header_.num_segments) {
    throw std::out_of_range("Segment index out of range");
  }

  const uint32_t mantissa_bits = 23;
  return 1 << (mantissa_bits - header_.m_bits);
}
