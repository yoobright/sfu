#include "chisel.h"
#include "cpu.h"
#include "error_stats.h"
#include "input_gen.h"
#include "mpfr_sfu.h"
#include "sfu_core.h"
#include "test_utils.h"
#include <cstring>
#include <iostream>
#include <random>

#ifdef USE_GPU
#include "gpu.h"
#endif

enum class ImplType { CHISEL, CMODEL, CPU, GPU, MPFR };

ImplType parse_impl_type(const char *str) {
  if (strcmp(str, "chisel") == 0)
    return ImplType::CHISEL;
  if (strcmp(str, "cmodel") == 0)
    return ImplType::CMODEL;
  if (strcmp(str, "cpu") == 0)
    return ImplType::CPU;
  if (strcmp(str, "gpu") == 0)
    return ImplType::GPU;
  if (strcmp(str, "mpfr") == 0)
    return ImplType::MPFR;
  std::cerr << "Unknown implementation: " << str << std::endl;
  exit(1);
}

SFUOp parse_op(const char *str) {
  if (strcmp(str, "exp2") == 0)
    return SFUOp::EXP2;
  if (strcmp(str, "log2") == 0)
    return SFUOp::LOG2;
  if (strcmp(str, "rcp") == 0)
    return SFUOp::RCP;
  if (strcmp(str, "sqrt") == 0)
    return SFUOp::SQRT;
  if (strcmp(str, "rsqrt") == 0)
    return SFUOp::RSQRT;
  if (strcmp(str, "sin") == 0)
    return SFUOp::SIN;
  if (strcmp(str, "cos") == 0)
    return SFUOp::COS;
  std::cerr << "Unknown operation: " << str << std::endl;
  exit(1);
}

void compute_batch(ImplType type, const std::vector<float> &inputs,
                   std::vector<float> &outputs, SFUOp op) {
  outputs.resize(inputs.size());

  switch (type) {
  case ImplType::CHISEL:
    Chisel::compute_batch(inputs.data(), outputs.data(), inputs.size(), op);
    break;

  case ImplType::CMODEL:
    for (size_t i = 0; i < inputs.size(); i++) {
      outputs[i] = SFUCore::compute(inputs[i], op);
    }
    break;

  case ImplType::CPU:
    CPU::compute_batch(inputs.data(), outputs.data(), inputs.size(), op);
    break;

#ifdef USE_GPU
  case ImplType::GPU:
    GPU::compute_batch(inputs.data(), outputs.data(), inputs.size(), op);
    break;
#endif

  case ImplType::MPFR:
    MPFR::compute_batch_float(inputs.data(), outputs.data(), inputs.size(), op);
    break;
  }
}

// void test_operation(SFUOp op, ImplType dut_type, ImplType ref_type) {
//   const char *op_names[] = {"EXP2",  "LOG2", "RCP", "SQRT",
//                             "RSQRT", "SIN",  "COS"};
//   const char *impl_names[] = {"CHISEL", "CMODEL", "CPU", "GPU", "MPFR"};
//
//   std::cout << "\n=== Testing " << op_names[static_cast<int>(op)]
//             << " (DUT: " << impl_names[static_cast<int>(dut_type)]
//             << ", REF: " << impl_names[static_cast<int>(ref_type)] << ")
//             ===\n";
//
//   // auto inputs = InputGen::generate(op);
//
//   // auto inputs = InputGen::generate_range(0x3e800000, 0x3f000000); //
//   // [0.25,0.5)
//   // auto inputs = InputGen::generate_range(0x3f000000, 0x3f800000); //
//   // [0.5,1.0)
//   // auto inputs = InputGen::generate_range(0x3f800000, 0x40000000); //
//   // [1.0,2.0)
//   // auto inputs = InputGen::generate_range(0x40000000, 0x40800000); //
//   // [2.0,4.0)
//   auto inputs = InputGen::generate_range(0xC0000000, 0xC0800000); //
//   // [-2.0,-4.0)
//   std::vector<float> dut_results, ref_results;
//
//   compute_batch(dut_type, inputs, dut_results, op);
//   compute_batch(ref_type, inputs, ref_results, op);
//
//   ErrorStats stats = ErrorComputer::compute_stats(
//       ref_results.data(), dut_results.data(), inputs.size());
//
//   ErrorComputer::print_stats("Result", stats);
// }

void test_operation(SFUOp op, ImplType dut_type, ImplType ref_type) {
  const char *op_names[] = {"EXP2",  "LOG2", "RCP", "SQRT",
                            "RSQRT", "SIN",  "COS"};
  const char *impl_names[] = {"CHISEL", "CMODEL", "CPU", "GPU", "MPFR"};

  const char *op_name = op_names[static_cast<int>(op)];
  const char *dut_name = impl_names[static_cast<int>(dut_type)];
  const char *ref_name = impl_names[static_cast<int>(ref_type)];

  std::cout << "\n=== Testing " << op_name << " (DUT: " << dut_name
            << ", REF: " << ref_name << ") ===\n";

  std::vector<float> inputs;
  inputs.reserve(254 * 1024 * 16);

  std::mt19937 rng(0x202512);
  std::uniform_int_distribution<uint32_t> mant_dist(0, (1u << 23) - 1);

  for (uint32_t exp = 1; exp <= 254; exp++) {
    for (int i = 0; i < 1024 * 16; i++) {
      uint32_t mant = mant_dist(rng);
      uint32_t bits = (exp << 23) | mant;
      float x;
      memcpy(&x, &bits, sizeof(float));
      inputs.push_back(x);
    }
  }

  std::vector<float> dut_results, ref_results;
  compute_batch(dut_type, inputs, dut_results, op);
  compute_batch(ref_type, inputs, ref_results, op);

  char csv_name[256];
  snprintf(csv_name, sizeof(csv_name), "result/%s_dut%s_ref%s_result.csv",
           op_name, dut_name, ref_name);

  FILE *csv = fopen(csv_name, "w");
  if (!csv) {
    perror("fopen");
    return;
  }

  fprintf(csv, "input,dut,ref,AbsErr,RelErr,ULP\n");

  for (size_t i = 0; i < inputs.size(); i++) {
    double g = TestUtils::float_to_double(ref_results[i]);
    double t = TestUtils::float_to_double(dut_results[i]);

    double abs_err = TestUtils::compute_abs_error(g, t);
    double rel_err = TestUtils::compute_rel_error(g, t);
    uint64_t ulp = TestUtils::compute_ulp(ref_results[i], dut_results[i]);

    fprintf(csv, "%.9e,%.9e,%.9e,%.6e,%.6e,%lu\n", inputs[i], dut_results[i],
            ref_results[i], abs_err, rel_err, ulp);
  }

  fclose(csv);
}

void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " [options]\n";
  std::cout << "Options:\n";
  std::cout
      << "  --dut <type>    DUT implementation (chisel|cmodel|cpu|gpu|mpfr)\n";
  std::cout << "  --ref <type>    Reference implementation "
               "(chisel|cmodel|cpu|gpu|mpfr)\n";
  std::cout
      << "  --op <op>       Operation (exp2|log2|rcp|sqrt|rsqrt|sin|cos|all)\n";
  std::cout << "  --help          Show this help\n";
}

int main(int argc, char **argv) {
  ImplType dut_type = ImplType::CHISEL;
  ImplType ref_type = ImplType::MPFR;
  std::string op_str = "all";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dut") == 0 && i + 1 < argc) {
      dut_type = parse_impl_type(argv[++i]);
    } else if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc) {
      ref_type = parse_impl_type(argv[++i]);
    } else if (strcmp(argv[i], "--op") == 0 && i + 1 < argc) {
      op_str = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
  }

  Chisel::init();
  SFUCore::init();

#ifdef USE_GPU
  if (dut_type == ImplType::GPU || ref_type == ImplType::GPU) {
    GPU::init();
  }
#endif

  if (op_str == "all") {
    for (int i = 0; i < 7; i++) {
      test_operation(static_cast<SFUOp>(i), dut_type, ref_type);
    }
  } else {
    test_operation(parse_op(op_str.c_str()), dut_type, ref_type);
  }

#ifdef USE_GPU
  if (dut_type == ImplType::GPU || ref_type == ImplType::GPU) {
    GPU::cleanup();
  }
#endif

  Chisel::cleanup();

  return 0;
}
