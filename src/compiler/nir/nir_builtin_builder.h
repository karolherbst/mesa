/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef NIR_BUILTIN_BUILDER_H
#define NIR_BUILTIN_BUILDER_H

#include "nir/nir_builder.h"

nir_ssa_def* nir_iadd_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_cross3(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_cross4(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_fast_length(nir_builder *b, nir_ssa_def *vec);
nir_ssa_def* nir_length(nir_builder *b, nir_ssa_def *vec);
nir_ssa_def* nir_normalize(nir_builder *b, nir_ssa_def *vec);
nir_ssa_def* nir_ihadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_uhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_irhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_urhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_rotate(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_smoothstep(nir_builder *b, nir_ssa_def *edge0,
                            nir_ssa_def *edge1, nir_ssa_def *x);
nir_ssa_def* nir_isub_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_iupsample(nir_builder *b, nir_ssa_def *hi, nir_ssa_def *lo);
nir_ssa_def* nir_uupsample(nir_builder *b, nir_ssa_def *hi, nir_ssa_def *lo);

static inline nir_ssa_def *
nir_iabs_diff(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *cond = nir_ige(b, x, y);
   nir_ssa_def *res0 = nir_isub(b, x, y);
   nir_ssa_def *res1 = nir_isub(b, y, x);
   return nir_bcsel(b, cond, res0, res1);
}

static inline nir_ssa_def *
nir_uabs_diff(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *cond = nir_uge(b, x, y);
   nir_ssa_def *res0 = nir_isub(b, x, y);
   nir_ssa_def *res1 = nir_isub(b, y, x);
   return nir_bcsel(b, cond, res0, res1);
}

static inline nir_ssa_def *
nir_uadd_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *sum = nir_iadd(b, x, y);
   nir_ssa_def *cond = nir_ult(b, sum, x);
   return nir_bcsel(b, cond, nir_imm_intN_t(b, -1l, x->bit_size), sum);
}

static inline nir_ssa_def *
nir_bitselect(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, nir_ssa_def *s)
{
   return nir_ior(b, nir_iand(b, nir_inot(b, s), x), nir_iand(b, s, y));
}

static inline nir_ssa_def *
nir_fclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_fmin(b, nir_fmax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_iclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_imin(b, nir_imax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_uclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_umin(b, nir_umax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_degrees(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_float(b, 57.2957795131));
}

static inline nir_ssa_def *
nir_degrees64(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_double(b, 57.29577951308232));
}

static inline nir_ssa_def *
nir_fast_distance(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_fast_length(b, nir_fsub(b, x, y));
}

static inline nir_ssa_def *
nir_distance(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_length(b, nir_fsub(b, x, y));
}

static inline nir_ssa_def*
nir_fast_normalize(nir_builder *b, nir_ssa_def *vec)
{
   return nir_fdiv(b, vec, nir_fast_length(b, vec));
}

static inline nir_ssa_def*
nir_mad(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, nir_ssa_def *z)
{
   return nir_iadd(b, nir_imul(b, x, y), z);
}

static inline nir_ssa_def *
nir_radians(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_float(b, 0.01745329251));
}

static inline nir_ssa_def *
nir_radians64(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_double(b, 0.017453292519943295));
}

static inline nir_ssa_def *
nir_select(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, nir_ssa_def *s)
{
   if (s->num_components != 1) {
      uint64_t mask = 1ul << (s->bit_size - 1);
      s = nir_iand(b, s, nir_imm_intN_t(b, mask, s->bit_size));
   }
   return nir_bcsel(b, nir_ieq(b, s, nir_imm_intN_t(b, 0, s->bit_size)), x, y);
}

static inline nir_ssa_def *
nir_usub_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *sum = nir_isub(b, x, y);
   nir_ssa_def *cond = nir_ult(b, x, y);
   return nir_bcsel(b, cond, nir_imm_intN_t(b, 0, x->bit_size), sum);
}


#endif /* NIR_BUILTIN_BUILDER_H */
