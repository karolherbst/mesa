/*
 * Copyright © 2018 Red Hat
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
 *
 * Authors:
 *    Rob Clark (robdclark@gmail.com)
 */

#include "math.h"

#include "nir/nir_builtin_builder.h"

#include "vtn_private.h"
#include "OpenCL.std.h"

typedef nir_ssa_def *(*nir_handler)(struct vtn_builder *b, enum OpenCLstd opcode,
                                    unsigned num_srcs, nir_ssa_def **srcs,
                                    const struct glsl_type *dest_type);

static void
handle_instr(struct vtn_builder *b, enum OpenCLstd opcode, const uint32_t *w,
             unsigned count, nir_handler handler)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   unsigned num_srcs = count - 5;
   nir_ssa_def *srcs[3] = { NULL };
   vtn_assert(num_srcs <= ARRAY_SIZE(srcs));
   for (unsigned i = 0; i < num_srcs; i++) {
      srcs[i] = vtn_ssa_value(b, w[i + 5])->def;
   }

   nir_ssa_def *result = handler(b, opcode, num_srcs, srcs, dest_type);
   if (result) {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = vtn_create_ssa_value(b, dest_type);
      val->ssa->def = result;
   } else {
      vtn_assert(dest_type == glsl_void_type());
   }
}

static nir_op
nir_alu_op_for_opencl_opcode(struct vtn_builder *b, enum OpenCLstd opcode)
{
   switch (opcode) {
   case Fabs: return nir_op_fabs;
   case SAbs: return nir_op_iabs;
   case Ceil: return nir_op_fceil;
   case Cos: return nir_op_fcos;
   case Exp2: return nir_op_fexp2;
   case Log2: return nir_op_flog2;
   case Floor: return nir_op_ffloor;
   case Fma: return nir_op_ffma;
   case Fmax: return nir_op_fmax;
   case SMax: return nir_op_imax;
   case UMax: return nir_op_umax;
   case Fmin: return nir_op_fmin;
   case SMin: return nir_op_imin;
   case UMin: return nir_op_umin;
   case Fmod: return nir_op_fmod;
   case Mix: return nir_op_flrp;
   case SMul_hi: return nir_op_imul_high;
   case UMul_hi: return nir_op_umul_high;
   case Popcount: return nir_op_bit_count;
   case Pow: return nir_op_fpow;
   case Remainder: return nir_op_frem;
   case Rsqrt: return nir_op_frsq;
   case Sign: return nir_op_fsign;
   case Sin: return nir_op_fsin;
   case Sqrt: return nir_op_fsqrt;
   case Trunc: return nir_op_ftrunc;
   /* uhm... */
   case UAbs: return nir_op_imov;
   default:
      vtn_fail("No NIR equivalent");
   }
}

static nir_ssa_def *
handle_alu(struct vtn_builder *b, enum OpenCLstd opcode, unsigned num_srcs,
           nir_ssa_def **srcs, const struct glsl_type *dest_type)
{
   return nir_build_alu(&b->nb, nir_alu_op_for_opencl_opcode(b, opcode),
                        srcs[0], srcs[1], srcs[2], NULL);
}

static nir_ssa_def *
handle_special(struct vtn_builder *b, enum OpenCLstd opcode, unsigned num_srcs,
               nir_ssa_def **srcs, const struct glsl_type *dest_type)
{
   nir_builder *nb = &b->nb;

   switch (opcode) {
   case SAbs_diff:
      return nir_iabs_diff(nb, srcs[0], srcs[1]);
   case UAbs_diff:
      return nir_uabs_diff(nb, srcs[0], srcs[1]);
   case SAdd_sat:
      return nir_iadd_sat(nb, srcs[0], srcs[1]);
   case UAdd_sat:
      return nir_uadd_sat(nb, srcs[0], srcs[1]);
   case Bitselect:
      return nir_bitselect(nb, srcs[0], srcs[1], srcs[2]);
   case FClamp:
      return nir_fclamp(nb, srcs[0], srcs[1], srcs[2]);
   case SClamp:
      return nir_iclamp(nb, srcs[0], srcs[1], srcs[2]);
   case UClamp:
      return nir_uclamp(nb, srcs[0], srcs[1], srcs[2]);
   case Copysign:
      return nir_copysign(nb, srcs[0], srcs[1]);
   case Cross:
      if (glsl_get_components(dest_type) == 4)
         return nir_cross4(nb, srcs[0], srcs[1]);
      return nir_cross3(nb, srcs[0], srcs[1]);
   case Degrees:
      return nir_degrees(nb, srcs[0]);
   case Fdim:
      return nir_fdim(nb, srcs[0], srcs[1]);
   case Distance:
      return nir_distance(nb, srcs[0], srcs[1]);
   case Fast_distance:
      return nir_fast_distance(nb, srcs[0], srcs[1]);
   case Fast_length:
      return nir_fast_length(nb, srcs[0]);
   case Fast_normalize:
      return nir_fast_normalize(nb, srcs[0]);
   case SHadd:
      return nir_ihadd(nb, srcs[0], srcs[1]);
   case UHadd:
      return nir_uhadd(nb, srcs[0], srcs[1]);
   case Length:
      return nir_length(nb, srcs[0]);
   case Mad:
      return nir_fmad(nb, srcs[0], srcs[1], srcs[2]);
   case Maxmag:
      return nir_maxmag(nb, srcs[0], srcs[1]);
   case Minmag:
      return nir_minmag(nb, srcs[0], srcs[1]);
   case Nan:
      return nir_nan(nb, srcs[0]);
   case Nextafter:
      return nir_nextafter(nb, srcs[0], srcs[1]);
   case Normalize:
      return nir_normalize(nb, srcs[0]);
   case Radians:
      return nir_radians(nb, srcs[0]);
   case SRhadd:
      return nir_irhadd(nb, srcs[0], srcs[1]);
   case URhadd:
      return nir_urhadd(nb, srcs[0], srcs[1]);
   case Rotate:
      return nir_rotate(nb, srcs[0], srcs[1]);
   case Smoothstep:
      return nir_smoothstep(nb, srcs[0], srcs[1], srcs[2]);
   case Select:
      return nir_select(nb, srcs[0], srcs[1], srcs[2]);
   case Step:
      return nir_sge(nb, srcs[1], srcs[0]);
   case SSub_sat:
      return nir_isub_sat(nb, srcs[0], srcs[1]);
   case USub_sat:
      return nir_usub_sat(nb, srcs[0], srcs[1]);
   case S_Upsample:
      return nir_iupsample(nb, srcs[0], srcs[1]);
   case U_Upsample:
      return nir_uupsample(nb, srcs[0], srcs[1]);
   default:
      vtn_fail("No NIR equivalent");
      return NULL;
   }
}

static nir_ssa_def *
handle_printf(struct vtn_builder *b, enum OpenCLstd opcode, unsigned num_srcs,
              nir_ssa_def **srcs, const struct glsl_type *dest_type)
{
   /* hahah, yeah, right.. */
   return nir_imm_int(&b->nb, -1);
}

bool
vtn_handle_opencl_instruction(struct vtn_builder *b, uint32_t ext_opcode,
                              const uint32_t *w, unsigned count)
{
   switch (ext_opcode) {
   case Fabs:
   case SAbs:
   case UAbs:
   case Ceil:
   case Cos:
   case Exp2:
   case Log2:
   case Floor:
   case Fma:
   case Fmax:
   case SMax:
   case UMax:
   case Fmin:
   case SMin:
   case UMin:
   case Mix:
   case Fmod:
   case SMul_hi:
   case UMul_hi:
   case Popcount:
   case Pow:
   case Remainder:
   case Rsqrt:
   case Sign:
   case Sin:
   case Sqrt:
   case Trunc:
      handle_instr(b, ext_opcode, w, count, handle_alu);
      return true;
   case SAbs_diff:
   case UAbs_diff:
   case SAdd_sat:
   case UAdd_sat:
   case Bitselect:
   case FClamp:
   case SClamp:
   case UClamp:
   case Copysign:
   case Cross:
   case Degrees:
   case Fdim:
   case Distance:
   case Fast_distance:
   case Fast_length:
   case Fast_normalize:
   case SHadd:
   case UHadd:
   case Length:
   case Mad:
   case Maxmag:
   case Minmag:
   case Nan:
   case Nextafter:
   case Normalize:
   case Radians:
   case SRhadd:
   case URhadd:
   case Rotate:
   case Select:
   case Step:
   case Smoothstep:
   case SSub_sat:
   case USub_sat:
   case S_Upsample:
   case U_Upsample:
      handle_instr(b, ext_opcode, w, count, handle_special);
      return true;
   case Printf:
      handle_instr(b, ext_opcode, w, count, handle_printf);
      return true;
   case Prefetch:
      /* TODO maybe add a nir instruction for this? */
      return true;
   default:
      vtn_fail("unhandled opencl opc: %u\n", ext_opcode);
      return false;
   }
}
