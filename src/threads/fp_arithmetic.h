#ifndef THREADS_FP_ARITHMETIC_H
#define THREADS_FP_ARITHMETIC_H

#include <stdint.h>

#define FIXPOINT_SHFT 14

#define fp_t int32_t
#define fp_lt int64_t

#define ONE_SIXTIETH 0x111        // ( 1 << 14) / 60 = 273.06667
#define FIFTYNINE_SIXTIETH 0x3EEF // (59 << 14) / 60 = 16110.933

fp_t int_to_fp (int);

int fp_to_int (fp_t);
int fp_rnd_int (fp_t);

fp_t fp_add (fp_t);
fp_t fp_sub (fp_t);
fp_t fp_mul (fp_t);
fp_t fp_div (fp_t);

#endif