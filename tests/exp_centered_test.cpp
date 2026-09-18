#include "sfu_core.h"
#include "sfu_filter.h"
#include "sfu_lut.h"
#include "sfu_poly.h"
#include "sfu_range_reduce.h"
#ifdef TEST_EXP_RTL
#include "chisel.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

static uint32_t bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
static float value(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
static uint32_t distance(float a, float b) {
  uint32_t x=bits(a), y=bits(b); return x>y ? x-y : y-x;
}
int main() {
  SFUCore::init();
#ifdef TEST_EXP_RTL
  Chisel::init();
#endif
  bool ok=true;
  // Magnitude uses all 17 bits at the left edge; sign describes the side,
  // including for a negative original input. Integer decomposition is intact.
  for (int integer : {-4, 0, 3}) {
    for (unsigned segment=0; segment<64; ++segment) {
      for (unsigned local : {0U, 1U, 65535U, 65536U, 65537U, 131071U}) {
        // x in [1,2) permits every Q23 residual to be represented exactly.
        float x=1.0f + (segment*131072U+local)*0x1p-23f;
        auto reduced=SFURangeReduce::reduce(SFUFilter::filter(bits(x),SFUOp::EXP2),SFUOp::EXP2);
        ok &= reduced.index==segment && reduced.exp==1;
        ok &= reduced.sign==(local<65536);
        ok &= reduced.xl==(local<65536 ? 65536-local : local-65536);
        auto lut=SFULUT::lookup(reduced,SFUOp::EXP2);
        ok &= lut.c0>=0 && lut.c0<(1<<26) && lut.c1>=0 && lut.c1<(1<<16) && lut.c2>=0 && lut.c2<(1<<12);
        ok &= (lut.c0<48408813)==(segment<34);
        // Check signed inputs independently against the numerical function.
        float shifted=integer+(segment+0.5f)/64.0f;
        ok &= distance(SFUCore::compute(shifted,SFUOp::EXP2),static_cast<float>(std::exp2(static_cast<double>(shifted))))<=1;
      }
    }
  }
  uint32_t max_ulp=0, previous=0;
  uint64_t sum_ulp=0, exact=0;
  double max_real_ulp=0.0;
  constexpr uint32_t count=1U<<23;
  for (uint32_t i=0;i<count;++i) {
    float x=value(0x3F800000U+i); // every FP32 value in [1,2)
    float actual=SFUCore::compute(x,SFUOp::EXP2);
    double ref=std::exp2(static_cast<double>(x));
    uint32_t d=distance(actual,static_cast<float>(ref));
    max_ulp=std::max(max_ulp,d); sum_ulp+=d; exact+=d==0;
    double real_ulp=std::abs(actual-ref)*0x1p22;
    max_real_ulp=std::max(max_real_ulp,real_ulp);
    if (bits(actual)<previous || d>1 || real_ulp>=1.0 || !std::isfinite(actual)) {
      std::cerr<<"EXP2 centered kernel failure at "<<std::hexfloat<<x<<'\n'; ok=false; break;
    }
    previous=bits(actual);
#ifdef TEST_EXP_RTL
    float rtl=Chisel::compute(x,SFUOp::EXP2);
    if (bits(rtl)!=bits(actual)) { std::cerr<<"EXP2 RTL mismatch at "<<x<<'\n'; ok=false; break; }
#endif
  }
  for (int i=-126;i<128;++i)
    ok &= bits(SFUCore::compute(static_cast<float>(i),SFUOp::EXP2))==bits(std::ldexp(1.0f,i));
  std::cout<<std::setprecision(9)<<"EXP2 Q23 kernel: samples="<<count<<" maxULP="<<max_ulp
           <<" maxRealULP="<<max_real_ulp<<" avgULP="<<double(sum_ulp)/count
           <<" referenceMatch="<<100.0*exact/count<<"%\n";
  std::cout<<"Centered endpoints, bias encoding, powers of two, monotonicity: "<<(ok?"PASS":"FAIL")<<'\n';
#ifdef TEST_EXP_RTL
  Chisel::cleanup();
#endif
  return ok?0:1;
}
