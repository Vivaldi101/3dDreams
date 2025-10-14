#if !defined(_FIXED_point_H)
#define _FIXED_POINT_H

#include "common.h"

static fp FP_to_fixed_point(f32 f, i32 scale) { return (fp)f << scale; }

static fp FP_fixed_add(fp a, fp b) { return a + b; }

static fp FP_fixed_sub(fp a, fp b) { return a - b; }

static fp FP_fixed_mul(fp a, fp b, i32 scale) { return (a * b) / scale; }

#endif
