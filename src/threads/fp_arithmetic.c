#include "threads/fp_arithmetic.h"

fp_t
int_to_fp (int i)
{
  return i << FIXPOINT_SHFT;
}

int
fp_to_int (fp_t fp)
{
  return fp >> FIXPOINT_SHFT;
}

int
fp_rnd_int (fp_t fp)
{
  if (fp >= 0)
    {
      return (fp + (1 << (FIXPOINT_SHFT - 1))) >> FIXPOINT_SHFT;
    }
  else
    {
      return (fp - (1 << (FIXPOINT_SHFT - 1))) >> FIXPOINT_SHFT;
    }
}

fp_t
fp_add (fp_t x, fp_t y)
{
  return x + y;
}

fp_t
fp_sub (fp_t x, fp_t y)
{
  return x - y;
}

fp_t
fp_mul (fp_t x, fp_t y)
{
  return (((fp_lt)x) * y) >> FIXPOINT_SHFT;
}

fp_t
fp_div (fp_t x, fp_t y)
{
  return (((fp_lt)x) << FIXPOINT_SHFT) / y;
}