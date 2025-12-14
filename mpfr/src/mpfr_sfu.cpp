#include "mpfr_sfu.h"
#include <cmath>
#include <mpfr.h>

static const int MPFR_PRECISION = 256;

float MPFR::compute_float(float input, SFUOp op) {
  mpfr_t x, result;
  mpfr_init2(x, MPFR_PRECISION);
  mpfr_init2(result, MPFR_PRECISION);

  mpfr_set_flt(x, input, MPFR_RNDN);

  switch (op) {
  case SFUOp::EXP2:
    mpfr_exp2(result, x, MPFR_RNDN);
    break;
  case SFUOp::LOG2:
    mpfr_log2(result, x, MPFR_RNDN);
    break;
  case SFUOp::RCP:
    mpfr_ui_div(result, 1, x, MPFR_RNDN);
    break;
  case SFUOp::SQRT:
    mpfr_sqrt(result, x, MPFR_RNDN);
    break;
  case SFUOp::RSQRT:
    mpfr_sqrt(result, x, MPFR_RNDN);
    mpfr_ui_div(result, 1, result, MPFR_RNDN);
    break;
  }

  float output = mpfr_get_flt(result, MPFR_RNDN);

  mpfr_clear(x);
  mpfr_clear(result);

  return output;
}

double MPFR::compute_double(double input, SFUOp op) {
  mpfr_t x, result;
  mpfr_init2(x, MPFR_PRECISION);
  mpfr_init2(result, MPFR_PRECISION);

  mpfr_set_d(x, input, MPFR_RNDN);

  switch (op) {
  case SFUOp::EXP2:
    mpfr_exp2(result, x, MPFR_RNDN);
    break;
  case SFUOp::LOG2:
    mpfr_log2(result, x, MPFR_RNDN);
    break;
  case SFUOp::RCP:
    mpfr_ui_div(result, 1, x, MPFR_RNDN);
    break;
  case SFUOp::SQRT:
    mpfr_sqrt(result, x, MPFR_RNDN);
    break;
  case SFUOp::RSQRT:
    mpfr_sqrt(result, x, MPFR_RNDN);
    mpfr_ui_div(result, 1, result, MPFR_RNDN);
    break;
  }

  double output = mpfr_get_d(result, MPFR_RNDN);

  mpfr_clear(x);
  mpfr_clear(result);

  return output;
}

void MPFR::compute_batch_float(const float *inputs, float *outputs, size_t n,
                               SFUOp op) {
  for (size_t i = 0; i < n; i++) {
    outputs[i] = compute_float(inputs[i], op);
  }
}

void MPFR::compute_batch_double(const double *inputs, double *outputs, size_t n,
                                SFUOp op) {
  for (size_t i = 0; i < n; i++) {
    outputs[i] = compute_double(inputs[i], op);
  }
}
