#include "sfu_lut.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

std::vector<LUTEntry> SFULUT::exp2_lut;
std::vector<LUTEntry> SFULUT::log2_lut;
std::vector<LUTEntry> SFULUT::rcp_lut;
std::vector<LUTEntry> SFULUT::sqrt_even_lut;
std::vector<LUTEntry> SFULUT::sqrt_odd_lut;
std::vector<LUTEntry> SFULUT::rsqrt_even_lut;
std::vector<LUTEntry> SFULUT::rsqrt_odd_lut;
std::vector<LUTEntry> SFULUT::sin_lut;
std::vector<LUTEntry> SFULUT::tanh_lut;

std::vector<LUTEntry> SFULUT::load_lut(const char *filename, int num_entries) {
  std::vector<LUTEntry> lut;
  std::ifstream file(filename);

  if (!file.is_open()) {
    printf("Warning: Could not open LUT file %s\n", filename);
    return lut;
  }

  std::string line;
  while (std::getline(file, line) && lut.size() < (size_t)num_entries) {
    std::istringstream iss(line);
    std::string c0_str, c1_str, c2_str;

    if (iss >> c0_str >> c1_str >> c2_str) {
      LUTEntry entry;
      entry.c0 = std::stoul(c0_str, nullptr, 16);
      entry.c1 = std::stoul(c1_str, nullptr, 16);
      entry.c2 = std::stoul(c2_str, nullptr, 16);
      lut.push_back(entry);
    }
  }

  return lut;
}

void SFULUT::init() {
  const char *lut_path = std::getenv("LUT_PATH");
  std::string base_path = lut_path ? lut_path : "../lut";

  exp2_lut = load_lut((base_path + "/exp2-coeffs.txt").c_str(), 64);
  log2_lut = load_lut((base_path + "/log2-coeffs.txt").c_str(), 64);
  rcp_lut = load_lut((base_path + "/rcp-coeffs.txt").c_str(), 128);
  sqrt_even_lut = load_lut((base_path + "/sqrt-even-coeffs.txt").c_str(), 64);
  sqrt_odd_lut = load_lut((base_path + "/sqrt-odd-coeffs.txt").c_str(), 64);
  rsqrt_even_lut = load_lut((base_path + "/rsqrt-even-coeffs.txt").c_str(), 64);
  rsqrt_odd_lut = load_lut((base_path + "/rsqrt-odd-coeffs.txt").c_str(), 64);
  sin_lut = load_lut((base_path + "/sin-coeffs.txt").c_str(), 64);
  tanh_lut = load_lut((base_path + "/tanh-coeffs.txt").c_str(), 128);
}

LUTOutput SFULUT::lookup(const RangeReduceOutput &input, SFUOp op) {
  LUTOutput out;
  out.xl = input.xl;
  out.sign = input.sign;
  out.exp = input.exp;

  const FunctionParams &params = Function::get(op);
  const std::vector<LUTEntry> *lut_ptr = nullptr;
  uint8_t index = input.index;

  switch (op) {
  case SFUOp::EXP2:
  case SFUOp::EXP:
    lut_ptr = &exp2_lut;
    index &= 0x3F;
    break;

  case SFUOp::LOG2:
    lut_ptr = &log2_lut;
    index &= 0x3F;
    break;

  case SFUOp::RCP:
    lut_ptr = &rcp_lut;
    break;

  case SFUOp::SQRT:
    lut_ptr = (input.index & 0x40) ? &sqrt_odd_lut : &sqrt_even_lut;
    index &= 0x3F;
    break;

  case SFUOp::RSQRT:
    lut_ptr = (input.index & 0x40) ? &rsqrt_odd_lut : &rsqrt_even_lut;
    index &= 0x3F;
    break;
  case SFUOp::SIN:
  case SFUOp::COS:
    lut_ptr = &sin_lut;
    index &= 0x3F;
    break;
  case SFUOp::SIGMOID:
  case SFUOp::TANH:
    lut_ptr = &tanh_lut;
    break;
  }

  if (lut_ptr && index < lut_ptr->size()) {
    const LUTEntry &entry = (*lut_ptr)[index];

    out.c0 = params.c0_sign * (int32_t)entry.c0;
    out.c1 = params.c1_sign * (int32_t)entry.c1;
    out.c2 = params.c2_sign * (int32_t)entry.c2;
  }

  return out;
}
