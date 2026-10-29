# CMSIS-DSP subset (vendored)

ARM's CMSIS-DSP, version 1.16.2 (https://github.com/ARM-software/CMSIS-DSP), reduced to what
`FftEngine` uses -- the single-precision real FFT and the magnitude computation:

- `src/`: `arm_rfft_fast_f32`, `arm_rfft_fast_init_f32`, `arm_cfft_f32`, `arm_cfft_init_f32`,
  `arm_cfft_radix8_f32`, `arm_bitreversal2`, `arm_common_tables`, `arm_const_structs`,
  `arm_cmplx_mag_f32`, `arm_scale_f32` (unchanged from 1.16.2).
- `include/`: the CMSIS-DSP headers (unchanged), plus `cmsis_compiler.h`, `cmsis_gcc.h` and
  `m-profile/cmsis_gcc_m.h` from CMSIS 6.1.0 (https://github.com/ARM-software/CMSIS_6).

Only the configured FFT size's tables are linked (`FftEngine` calls the size-specific init).
Built with `-O3 -DARM_MATH_LOOPUNROLL` (see `library.json`).

Licence: Apache License 2.0 (see `LICENSE`).

`arm_cmplx_mag_f32` takes its square roots with the FPU's `VSQRT` instruction (CMSIS's
`arm_sqrt_f32`), unlike a plain `sqrtf()`, which this build turns into a slow library call.
