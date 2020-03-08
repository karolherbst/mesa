/*
 * Copyright © 2020 Red Hat, Inc.
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
 * Authors (Collabora):
 *    Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "nir.h"
#include "nir_builder.h"

/*
 * Lowers atomics to CAS and loop.
 *
 * The idea here is simple:
 * 1. load the value
 * 2. do the operation
 * 3. cas the new value
 * 4. compare if the returned value was the oringinal loaded one
 * 5. repeat from 2. until match
 *
 * So far only 64 bit shared atomics are lowered.
 */

static nir_op
lower_atomic_op(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_shared_atomic_add:
      return nir_op_iadd;
   case nir_intrinsic_shared_atomic_and:
      return nir_op_iand;
   case nir_intrinsic_shared_atomic_imax:
      return nir_op_imax;
   case nir_intrinsic_shared_atomic_imin:
      return nir_op_imin;
   case nir_intrinsic_shared_atomic_umax:
      return nir_op_umax;
   case nir_intrinsic_shared_atomic_umin:
      return nir_op_umin;
   case nir_intrinsic_shared_atomic_or:
      return nir_op_ior;
   case nir_intrinsic_shared_atomic_xor:
      return nir_op_ixor;
   default:
      unreachable("Invalid SSBO op");
   }
}

static bool
should_lower_atomic_instr(const nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   const nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
      break;
   default:
      return false;
   }

   if (nir_dest_bit_size(intr->dest) < 64)
      return false;

   return true;
}

static void
lower_atomic_instr(nir_builder *b, nir_intrinsic_instr *atomic)
{
   nir_op op = lower_atomic_op(atomic->intrinsic);

   b->cursor = nir_before_instr(&atomic->instr);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_shared);

   load->num_components = 1;
   nir_intrinsic_set_base(load, nir_intrinsic_base(atomic));
   nir_intrinsic_set_align_mul(load, 8);
   load->src[0] = atomic->src[0];

   nir_ssa_dest_init(&load->instr, &load->dest, 1, 64, NULL);
   nir_builder_instr_insert(b, &load->instr);

   nir_ssa_def *operand = nir_ssa_for_src(b, atomic->src[1], 1);
   nir_loop *loop = nir_push_loop(b);

   /* phi for the load/cas dest */
   nir_phi_instr *phi = nir_phi_instr_create(b->shader);

   nir_phi_src *src = ralloc(phi, nir_phi_src);
   src->pred = load->instr.block;
   src->src = nir_src_for_ssa(&load->dest.ssa);
   exec_list_push_tail(&phi->srcs, &src->node);

   nir_ssa_dest_init(&phi->instr, &phi->dest, 1, 64, NULL);

   /* do the operation */
   nir_ssa_def *res = nir_build_alu(b, op, &phi->dest.ssa, operand, NULL, NULL);

   /* do the cas */
   nir_intrinsic_instr *cas =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_shared_atomic_comp_swap);
   cas->num_components = 1;
   nir_intrinsic_set_base(cas, nir_intrinsic_base(atomic));
   cas->src[0] = atomic->src[0];
   cas->src[1] = nir_src_for_ssa(&phi->dest.ssa);
   cas->src[2] = nir_src_for_ssa(res);
   nir_ssa_dest_init(&cas->instr, &cas->dest, 1, 64, NULL);
   nir_builder_instr_insert(b, &cas->instr);

   /* insert second phi source and insert the phi */
   src = ralloc(phi, nir_phi_src);
   src->pred = nir_loop_last_block(loop);
   src->src = nir_src_for_ssa(&cas->dest.ssa);
   exec_list_push_tail(&phi->srcs, &src->node);

   b->cursor = nir_before_instr(res->parent_instr);
   nir_builder_instr_insert(b, &phi->instr);

   b->cursor = nir_after_instr(&cas->instr);

   /* check against old result */
   nir_ssa_def *cond = nir_ieq(b, &cas->dest.ssa, &phi->dest.ssa);

   nir_if *nif = nir_push_if(b, cond);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, nif);

   nir_pop_loop(b, loop);

   /* the result of the last cas is the final one */
   nir_ssa_def_rewrite_uses(&atomic->dest.ssa, nir_src_for_ssa(&cas->dest.ssa));
   nir_instr_remove(&atomic->instr);
}

bool
nir_lower_atomics(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      nir_function_impl *impl = function->impl;
      nir_builder b;
      nir_builder_init(&b, impl);

      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (!should_lower_atomic_instr(instr))
               continue;

            progress = true;
            lower_atomic_instr(&b, nir_instr_as_intrinsic(instr));
         }
      }
   }

   return progress;
}
