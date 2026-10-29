#pragma once
#include <cmath>

// Square root on the FPU's VSQRT instruction (~14 cycles on the M33).
//
// Plain sqrtf() is NOT that here: the build compiles with -fno-builtin, so
// every sqrtf() becomes a call into the C library's software square root --
// a few hundred cycles. In the FFT magnitudes that alone cost 7.5 ms per FFT
// run. This is what CMSIS-DSP's arm_sqrt_f32() does too. x must be >= 0
// (a negative x gives NaN, with no errno).
inline float fastSqrt(float x) {
#if defined(__GNUC__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
    float r;
    __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    return sqrtf(x);   // host builds and targets without an FPU
#endif
}
