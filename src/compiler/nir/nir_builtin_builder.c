/*
 * Copyright © 2018 Red Hat Inc.
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

#include <math.h>

#include "nir.h"
#include "nir_builtin_builder.h"

#define NIR_IMM_FP(n, s, v) (s->bit_size == 64 ? nir_imm_double(n, v) : nir_imm_float(n, v))

nir_ssa_def*
nir_cross3(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   unsigned yzx[4] = { 1, 2, 0, 0 };
   unsigned zxy[4] = { 2, 0, 1, 0 };

   return nir_fsub(b, nir_fmul(b, nir_swizzle(b, x, yzx, 3, true),
                                  nir_swizzle(b, y, zxy, 3, true)),
                      nir_fmul(b, nir_swizzle(b, x, zxy, 3, true),
                                  nir_swizzle(b, y, yzx, 3, true)));
}

nir_ssa_def*
nir_cross4(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *cross = nir_cross3(b, x, y);

   return nir_vec4(b,
      nir_channel(b, cross, 0),
      nir_channel(b, cross, 1),
      nir_channel(b, cross, 2),
      nir_imm_intN_t(b, 0, cross->bit_size));
}

nir_ssa_def*
nir_fast_length(nir_builder *b, nir_ssa_def *vec)
{
   switch (vec->num_components) {
   case 1: return nir_fsqrt(b, nir_fmul(b, vec, vec));
   case 2: return nir_fsqrt(b, nir_fdot2(b, vec, vec));
   case 3: return nir_fsqrt(b, nir_fdot3(b, vec, vec));
   case 4: return nir_fsqrt(b, nir_fdot4(b, vec, vec));
   default:
      unreachable("Invalid number of components");
   }
}

nir_ssa_def*
nir_smoothstep(nir_builder *b, nir_ssa_def *edge0, nir_ssa_def *edge1, nir_ssa_def *x)
{
   nir_ssa_def *f2 = NIR_IMM_FP(b, x, 2.0);
   nir_ssa_def *f3 = NIR_IMM_FP(b, x, 3.0);

   /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1) */
   nir_ssa_def *t =
      nir_fsat(b, nir_fdiv(b, nir_fsub(b, x, edge0),
                              nir_fsub(b, edge1, edge0)));

   /* result = t * t * (3 - 2 * t) */
   return nir_fmul(b, t, nir_fmul(b, t, nir_fsub(b, f3, nir_fmul(b, f2, t))));
}

nir_ssa_def*
nir_length(nir_builder *b, nir_ssa_def *vec)
{
   nir_ssa_def *finf = NIR_IMM_FP(b, vec, INFINITY);

   nir_ssa_def *abs = nir_fabs(b, vec);
   if (vec->num_components == 1)
      return abs;

   nir_ssa_def *maxc = nir_fmax(b, nir_channel(b, abs, 0), nir_channel(b, abs, 1));
   for (int i = 2; i < vec->num_components; ++i)
      maxc = nir_fmax(b, maxc, nir_channel(b, abs, i));
   abs = nir_fdiv(b, abs, maxc);
   nir_ssa_def *res = nir_fmul(b, nir_fsqrt(b, nir_fdot(b, abs, abs)), maxc);
   return nir_bcsel(b, nir_feq(b, maxc, finf), maxc, res);
}

static nir_ssa_def*
nir_hadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, bool sign)
{
   nir_ssa_def *imm1 = nir_imm_intN_t(b, 1, x->bit_size);

   nir_ssa_def *t0 = nir_ixor(b, x, y);
   nir_ssa_def *t1 = nir_iand(b, x, y);

   nir_ssa_def *t2;
   if (sign)
      t2 = nir_ishr(b, t0, imm1);
   else
      t2 = nir_ushr(b, t0, imm1);
   return nir_iadd(b, t1, t2);
}

nir_ssa_def*
nir_ihadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_hadd(b, x, y, true);
}

nir_ssa_def*
nir_uhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_hadd(b, x, y, false);
}

static nir_ssa_def*
nir_rhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, bool sign)
{
   nir_ssa_def *imm1 = nir_imm_intN_t(b, 1, x->bit_size);

   nir_ssa_def *t0 = nir_ixor(b, x, y);
   nir_ssa_def *t1 = nir_ior(b, x, y);

   nir_ssa_def *t2;
   if (sign)
      t2 = nir_ishr(b, t0, imm1);
   else
      t2 = nir_ushr(b, t0, imm1);

   return nir_isub(b, t1, t2);
}

nir_ssa_def*
nir_irhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_rhadd(b, x, y, true);
}

nir_ssa_def*
nir_urhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_rhadd(b, x, y, false);
}

nir_ssa_def*
nir_normalize(nir_builder *b, nir_ssa_def *vec)
{
   bool is64bit = vec->bit_size == 64;

   nir_ssa_def *f0 = is64bit ? nir_imm_double(b,  0.0) : nir_imm_float(b,  0.0f);

   nir_ssa_def *maxc;
   nir_ssa_def *res;
   if (vec->num_components == 1) {
      nir_ssa_def *f1p = is64bit ? nir_imm_double(b,  1.0) : nir_imm_float(b,  1.0f);
      nir_ssa_def *f1n = is64bit ? nir_imm_double(b, -1.0) : nir_imm_float(b, -1.0f);

      nir_ssa_def *cond = nir_flt(b, vec, f0);
      res = nir_bcsel(b, cond, f1n, f1p);
      maxc = vec;
   } else {
      maxc = nir_fmax(b, nir_fabs(b, nir_channel(b, vec, 0)), nir_fabs(b, nir_channel(b, vec, 1)));
      for (int i = 2; i < vec->num_components; ++i)
         maxc = nir_fmax(b, maxc, nir_fabs(b, nir_channel(b, vec, i)));
      nir_ssa_def *temp = nir_fdiv(b, vec, maxc);
      res = nir_fmul(b, temp, nir_frsq(b, nir_fdot(b, temp, temp)));
   }
   return nir_bcsel(b, nir_feq(b, maxc, f0), vec, res);
}
