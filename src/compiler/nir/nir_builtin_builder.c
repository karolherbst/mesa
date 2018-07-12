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

nir_ssa_def*
nir_iadd_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   int64_t max;
   switch (x->bit_size) {
   case 64:
      max = INT64_MAX;
      break;
   case 32:
      max = INT32_MAX;
      break;
   case 16:
      max = INT16_MAX;
      break;
   case  8:
      max = INT8_MAX;
      break;
   }

   nir_ssa_def *sum = nir_iadd(b, x, y);

   nir_ssa_def *hi = nir_bcsel(b, nir_ilt(b, sum, x),
                               nir_imm_intN_t(b, max, x->bit_size), sum);

   nir_ssa_def *lo = nir_bcsel(b, nir_ilt(b, x, sum),
                               nir_imm_intN_t(b, max + 1, x->bit_size), sum);

   return nir_bcsel(b, nir_ige(b, y, nir_imm_intN_t(b, 1, y->bit_size)), hi, lo);
}

nir_ssa_def*
nir_cross3(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   unsigned yzx[3] = { 1, 2, 0 };
   unsigned zxy[3] = { 2, 0, 1 };

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

static nir_ssa_def*
nir_hadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, bool sign)
{
   nir_ssa_def *imm1 = nir_imm_int(b, 1);

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

nir_ssa_def*
nir_length(nir_builder *b, nir_ssa_def *vec)
{
   nir_ssa_def *finf = nir_imm_floatN_t(b, INFINITY, vec->bit_size);

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
nir_nextafter(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *zero = nir_imm_intN_t(b, 0, x->bit_size);
   nir_ssa_def *one = nir_imm_intN_t(b, 1, x->bit_size);
   nir_ssa_def *nzero = nir_imm_intN_t(b, 1ull << (x->bit_size - 1), x->bit_size);

   nir_ssa_def *condeq = nir_feq(b, x, y);
   nir_ssa_def *conddir = nir_flt(b, x, y);
   nir_ssa_def *condnzero = nir_feq(b, x, nzero);

   // beware of -0.0 - 1 == NaN
   nir_ssa_def *xn =
      nir_bcsel(b,
                condnzero,
                nir_imm_intN_t(b, (1 << (x->bit_size - 1)) + 1, x->bit_size),
                nir_isub(b, x, one));

   // beware of -0.0 + 1 == -0x1p-149
   nir_ssa_def *xp = nir_bcsel(b, condnzero, one, nir_iadd(b, x, one));

   // nextafter can be implemented by just +/- 1 on the int value
   nir_ssa_def *resp = nir_bcsel(b, conddir, xp, xn);
   nir_ssa_def *resn = nir_bcsel(b, conddir, xn, xp);

   nir_ssa_def *res = nir_bcsel(b, nir_flt(b, x, zero), resn, resp);

   return nir_nan_check2(b, x, y, nir_bcsel(b, condeq, x, res));
}

nir_ssa_def*
nir_normalize(nir_builder *b, nir_ssa_def *vec)
{
   nir_ssa_def *f0 = nir_imm_floatN_t(b, 0.0, vec->bit_size);

   nir_ssa_def *maxc;
   nir_ssa_def *res;
   if (vec->num_components == 1) {
      nir_ssa_def *f1p = nir_imm_floatN_t(b,  1.0, vec->bit_size);
      nir_ssa_def *f1n = nir_imm_floatN_t(b, -1.0, vec->bit_size);

      nir_ssa_def *cond = nir_flt(b, vec, f0);
      res = nir_bcsel(b, cond, f1n, f1p);
      maxc = vec;
   } else {
      maxc = nir_fmax(b, nir_fabs(b, nir_channel(b, vec, 0)),
                         nir_fabs(b, nir_channel(b, vec, 1)));
      for (int i = 2; i < vec->num_components; ++i)
         maxc = nir_fmax(b, maxc, nir_fabs(b, nir_channel(b, vec, i)));
      nir_ssa_def *temp = nir_fdiv(b, vec, maxc);
      res = nir_fmul(b, temp, nir_frsq(b, nir_fdot(b, temp, temp)));
   }
   return nir_bcsel(b, nir_feq(b, maxc, f0), vec, res);
}

static nir_ssa_def*
nir_rhadd(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y, bool sign)
{
   nir_ssa_def *imm1 = nir_imm_int(b, 1);

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
nir_rotate(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *shift_mask = nir_imm_int(b, x->bit_size - 1);

   if (y->bit_size != 32)
      y = nir_u2u32(b, y);

   nir_ssa_def *lshift = nir_iand(b, y, shift_mask);
   nir_ssa_def *rshift = nir_isub(b, nir_imm_int(b, x->bit_size), lshift);

   nir_ssa_def *hi = nir_ishl(b, x, lshift);
   nir_ssa_def *lo = nir_ushr(b, x, rshift);

   return nir_ior(b, hi, lo);
}

nir_ssa_def*
nir_smoothstep(nir_builder *b, nir_ssa_def *edge0, nir_ssa_def *edge1, nir_ssa_def *x)
{
   nir_ssa_def *f2 = nir_imm_floatN_t(b, 2.0, x->bit_size);
   nir_ssa_def *f3 = nir_imm_floatN_t(b, 3.0, x->bit_size);

   /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1) */
   nir_ssa_def *t =
      nir_fsat(b, nir_fdiv(b, nir_fsub(b, x, edge0),
                              nir_fsub(b, edge1, edge0)));

   /* result = t * t * (3 - 2 * t) */
   return nir_fmul(b, t, nir_fmul(b, t, nir_fsub(b, f3, nir_fmul(b, f2, t))));
}

nir_ssa_def*
nir_isub_sat(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   uint64_t max = (1ull << (x->bit_size - 1)) - 1;

   nir_ssa_def *diff = nir_isub(b, x, y);
   nir_ssa_def *hi = nir_bcsel(b, nir_ilt(b, diff, x),
                               nir_imm_intN_t(b, max, x->bit_size), diff);
   nir_ssa_def *lo = nir_bcsel(b, nir_ilt(b, x, diff),
                               nir_imm_intN_t(b, max + 1, x->bit_size), diff);
   return nir_bcsel(b, nir_ilt(b, y, nir_imm_intN_t(b, 0, y->bit_size)), hi, lo);
}

nir_ssa_def*
nir_iupsample(nir_builder *b, nir_ssa_def *hi, nir_ssa_def *lo)
{
   nir_ssa_def *hiup;
   nir_ssa_def *loup;
   switch (hi->bit_size) {
   case 32:
      hiup = nir_i2i64(b, hi);
      loup = nir_i2i64(b, lo);
      break;
   case 16:
      hiup = nir_i2i32(b, hi);
      loup = nir_i2i32(b, lo);
      break;
   case  8:
      hiup = nir_i2i16(b, hi);
      loup = nir_i2i16(b, lo);
      break;
   }
   return nir_ior(b, nir_ishl(b, hiup, nir_imm_int(b, hi->bit_size)), loup);
}

nir_ssa_def*
nir_uupsample(nir_builder *b, nir_ssa_def *hi, nir_ssa_def *lo)
{
   nir_ssa_def *hiup;
   nir_ssa_def *loup;
   switch (hi->bit_size) {
   case 32:
      hiup = nir_u2u64(b, hi);
      loup = nir_u2u64(b, lo);
      break;
   case 16:
      hiup = nir_u2u32(b, hi);
      loup = nir_u2u32(b, lo);
      break;
   case  8:
      hiup = nir_u2u16(b, hi);
      loup = nir_u2u16(b, lo);
      break;
   }
   return nir_ior(b, nir_ishl(b, hiup, nir_imm_int(b, hi->bit_size)), loup);
}
