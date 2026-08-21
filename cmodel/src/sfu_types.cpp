#include "sfu_types.h"

const FunctionParams Function::EXP2 = {6, 1, 1, 1, 1, 1, -1};
const FunctionParams Function::LOG2 = {6, 1, 1, -1, 0, 1, 0};
const FunctionParams Function::RCP = {7, 1, -1, 1, 0, 0, 0};
const FunctionParams Function::SQRT = {6, 1, 1, -1, 0, -1, -3};
const FunctionParams Function::RSQRT = {6, 1, -1, 1, 0, -1, -1};
const FunctionParams Function::SIN = {6, 1, 1, -1, 0, 1, 1};
// Approximate sigmoid(-|x|) over |x| / 8 in [0, 1). C0 uses Q0.26;
// C1/C2 retain enough headroom for derivatives with respect to |x| / 8.
const FunctionParams Function::SIGMOID = {7, 1, -1, 1, -1, 1, 1};

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
  default:
    return EXP2;
  }
}
