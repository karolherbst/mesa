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

#include "vtn_private.h"
#include "OpenCL.std.h"

typedef nir_ssa_def *(*nir_handler)(struct vtn_builder *b, enum OpenCLstd opcode,
                                    unsigned num_srcs, nir_ssa_def **srcs);

static void
handle_instr(struct vtn_builder *b, enum OpenCLstd opcode, const uint32_t *w,
             unsigned count, nir_handler handler)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   unsigned num_srcs = count - 5;
   nir_ssa_def *srcs[3] = { NULL, };
   vtn_assert(num_srcs <= ARRAY_SIZE(srcs));
   for (unsigned i = 0; i < num_srcs; i++) {
      srcs[i] = vtn_ssa_value(b, w[i + 5])->def;
   }

   nir_ssa_def *result = handler(b, opcode, num_srcs, srcs);
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
   case SHadd: return nir_op_ihadd;
   case UHadd: return nir_op_uhadd;
   default:
      vtn_fail("No NIR equivalent");
   }
}

static nir_ssa_def *
handle_alu(struct vtn_builder *b, enum OpenCLstd opcode, unsigned num_srcs,
           nir_ssa_def **srcs)
{
   return nir_build_alu(&b->nb, nir_alu_op_for_opencl_opcode(b, opcode),
                        srcs[0], srcs[1], srcs[2], NULL);
}

static nir_ssa_def *
handle_printf(struct vtn_builder *b, enum OpenCLstd opcode, unsigned num_srcs,
              nir_ssa_def **srcs)
{
   /* hahah, yeah, right.. */
   return nir_imm_int(&b->nb, -1);
}

/* Generate vload/vstore deref, with special handling for the cases
 * where src/dest type may not match what is in memory:
 *  * 'half' variants are stored in half precision in memory
 *  * 'a' variants pad vec3 to vec4
 * For the vec3 cases, other than the 'a' variants, this doesn't
 * match the normal rules (ie. that vec3 is padded/aligned to vec4)
 * so it deref's as an arr[n] rather than vecn, and then casts to the
 * proper types.
 */
static nir_deref_instr *
vloadstore_deref(struct vtn_builder *b, bool half, bool a,
                 nir_ssa_def *p, nir_ssa_def *offset,
                 const struct glsl_type *type)
{
   unsigned num_components = glsl_get_vector_elements(type);
   enum glsl_base_type bt = glsl_get_base_type(type);

   if (a && (num_components == 3))
      num_components = 4;

   if (half)
      bt = GLSL_TYPE_FLOAT16;

   const struct glsl_type *deref_type =
      glsl_array_type(glsl_scalar_type(bt), num_components);

   nir_deref_instr *deref =
      nir_build_deref_cast(&b->nb, p, nir_var_pointer, deref_type);
   deref = nir_build_deref_ptr_as_array(&b->nb, deref, offset);

   nir_ssa_def *fptr = nir_address_from_deref(&b->nb, deref);

   return nir_build_deref_cast(&b->nb, fptr, nir_var_pointer, type);
}

static void
vtn_handle_opencl_vload(struct vtn_builder *b, enum OpenCLstd opcode,
                        const uint32_t *w, unsigned count)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;
   unsigned num_components;
   bool half;

   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   nir_ssa_def *offset = vtn_ssa_value(b, w[5])->def;
   nir_ssa_def *p      = vtn_ssa_value(b, w[6])->def;

   p = nir_address_from_ssa(&b->nb, p);

   switch (opcode) {
   case Vload_half:
   case Vload_halfn:
   case Vloada_halfn:
      half = true;
      break;
   default:
      half = false;
      break;
   }

   if (count >= 7) {
      num_components = w[7];
   } else {
      num_components = 1;
   }

   vtn_assert(num_components == glsl_get_vector_elements(dest_type));

   bool a = (opcode == Vloada_halfn);
   nir_deref_instr *deref = vloadstore_deref(b, half, a, p, offset, dest_type);

   val->ssa->def = nir_load_deref(&b->nb, deref);

   if (half) {
      /* convert f16->f32: */
      val->ssa->def = nir_f2f32(&b->nb, val->ssa->def);
   }
}

static void
vtn_handle_opencl_vstore(struct vtn_builder *b, enum OpenCLstd opcode,
                         const uint32_t *w, unsigned count)
{
   const struct glsl_type *src_type = vtn_ssa_value(b, w[5])->type;
   bool half;

   nir_ssa_def *data   = vtn_ssa_value(b, w[5])->def;
   nir_ssa_def *offset = vtn_ssa_value(b, w[6])->def;
   nir_ssa_def *p      = vtn_ssa_value(b, w[7])->def;

   p = nir_address_from_ssa(&b->nb, p);

   switch (opcode) {
   case Vstore_half_r:
   case Vstore_halfn_r:
   case Vstorea_halfn_r:
      half = true;
      switch (w[8]) {
      case SpvFPRoundingModeRTE:
         data = nir_f2f16_rtne(&b->nb, data);
         break;
      case SpvFPRoundingModeRTZ:
         data = nir_f2f16_rtz(&b->nb, data);
         break;
      case SpvFPRoundingModeRTP:
      case SpvFPRoundingModeRTN:
      default:
         vtn_fail("unsupported rounding mode: %u\n", w[8]);
         break;
      }
      break;
   case Vstore_half:
   case Vstore_halfn:
   case Vstorea_halfn:
      half = true;
      data = nir_f2f16_undef(&b->nb, data);
      break;
   default:
      half = false;
      break;
   }

   vtn_assert(data->num_components == glsl_get_vector_elements(src_type));

   bool a = (opcode == Vstorea_halfn_r) || (opcode == Vstorea_halfn);
   nir_deref_instr *deref = vloadstore_deref(b, half, a, p, offset, src_type);

   unsigned wrmask = (1 << data->num_components) - 1;
   nir_store_deref(&b->nb, deref, data, wrmask);
}

bool
vtn_handle_opencl_instruction(struct vtn_builder *b, uint32_t ext_opcode,
                              const uint32_t *w, unsigned count)
{
   switch (ext_opcode) {
   case SHadd:
   case UHadd:
      handle_instr(b, ext_opcode, w, count, handle_alu);
      return true;
   case Vloadn:
   case Vload_half:
   case Vload_halfn:
   case Vloada_halfn:
      vtn_handle_opencl_vload(b, ext_opcode, w, count);
      return true;
   case Vstoren:
   case Vstore_half:
   case Vstore_half_r:
   case Vstore_halfn:
   case Vstore_halfn_r:
   case Vstorea_halfn:
   case Vstorea_halfn_r:
      vtn_handle_opencl_vstore(b, ext_opcode, w, count);
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
