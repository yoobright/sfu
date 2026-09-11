#include "sfu_types.h"

const FunctionParams Function::EXP2 = {6, 1, 1, 1, 1, 1, -1};
const FunctionParams Function::LOG2 = {6, 1, 1, -1, 0, 1, 0};
const FunctionParams Function::RCP = {7, 1, -1, 1, 0, 0, 0};
const FunctionParams Function::SQRT = {6, 1, 1, -1, 0, -1, -3};
const FunctionParams Function::RSQRT = {6, 1, -1, 1, 0, -1, -1};
const FunctionParams Function::SIN = {6, 1, 1, -1, 0, 1, 1};
// TANH and SIGMOID share a Q0.26 approximation of tanh(|x|).  SIGMOID
// supplies |x|/2 during range reduction and reconstructs (1 +/- tanh)/2.
const FunctionParams Function::TANH = {7, 1, 1, -1, -1, 1, 4};
const FunctionParams Function::SIGMOID = Function::TANH;
// EXP reuses the EXP2 interpolation table after x * log2(e) range reduction.
const FunctionParams Function::EXP = {6, 1, 1, 1, 1, 1, -1};

const FunctionParams &Function::get(SFUOp op) {
  switch (op) {
  case SFUOp::EXP2:
    return EXP2;
  case SFUOp::LOG2:
    return LOG2;
  case SFUOp::RCP:
    return RCP;
  case SFUOp::SQRT:
    return SQRT;
  case SFUOp::RSQRT:
    return RSQRT;
  case SFUOp::SIN:
    return SIN;
  case SFUOp::COS:
    return SIN;
  case SFUOp::SIGMOID:
    return SIGMOID;
  case SFUOp::EXP:
    return EXP;
  case SFUOp::TANH:
    return TANH;
  default:
    return EXP2;
  }
}
