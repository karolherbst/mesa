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

#include "util/u_math.h"

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
get_atomic_op(nir_intrinsic_op op)
{
   #define CASE(op) \
   case nir_intrinsic_deref_atomic_##op: \
   case nir_intrinsic_global_atomic_##op: \
   case nir_intrinsic_shared_atomic_##op: \
   case nir_intrinsic_ssbo_atomic_##op

   switch (op) {
   CASE(add):
      return nir_op_iadd;
   CASE(fadd):
      return nir_op_fadd;
   CASE(and):
      return nir_op_iand;
   CASE(or):
      return nir_op_ior;
   CASE(xor):
      return nir_op_ixor;
   CASE(fmax):
      return nir_op_fmax;
   CASE(imax):
      return nir_op_imax;
   CASE(umax):
      return nir_op_umax;
   CASE(fmin):
      return nir_op_fmin;
   CASE(imin):
      return nir_op_imin;
   CASE(umin):
      return nir_op_umin;

   /* TODO */
   CASE(comp_swap):
   CASE(exchange):
   CASE(fcomp_swap):
   default:
      return nir_num_opcodes;
   }
   #undef CASE
}

#define CASE(type)                       \
case nir_intrinsic_##type##_atomic_add:  \
case nir_intrinsic_##type##_atomic_and:  \
case nir_intrinsic_##type##_atomic_fadd: \
case nir_intrinsic_##type##_atomic_fmax: \
case nir_intrinsic_##type##_atomic_fmin: \
case nir_intrinsic_##type##_atomic_imax: \
case nir_intrinsic_##type##_atomic_imin: \
case nir_intrinsic_##type##_atomic_or:   \
case nir_intrinsic_##type##_atomic_umax: \
case nir_intrinsic_##type##_atomic_umin: \
case nir_intrinsic_##type##_atomic_xor

static nir_intrinsic_op
get_atomic_load(nir_intrinsic_op op)
{
   switch (op) {
   CASE(deref):
      return nir_intrinsic_load_deref;
   CASE(global):
      return nir_intrinsic_load_global;
   CASE(shared):
      return nir_intrinsic_load_shared;
   CASE(ssbo):
      return nir_intrinsic_load_ssbo;
   default:
      unreachable("Invalid atomic op");
      assert(false);
      return nir_num_opcodes;
   }
}

static nir_intrinsic_op
get_atomic_cas(nir_intrinsic_op op)
{
   switch (op) {
   CASE(deref):
      return nir_intrinsic_deref_atomic_comp_swap;
   CASE(global):
      return nir_intrinsic_global_atomic_comp_swap;
   CASE(shared):
      return nir_intrinsic_shared_atomic_comp_swap;
   CASE(ssbo):
      return nir_intrinsic_ssbo_atomic_comp_swap;
   default:
      unreachable("Invalid atomic op");
      assert(false);
      return nir_num_opcodes;
   }
}
#undef CASE

static bool
should_lower_atomic_instr(const nir_instr *instr, const void *_state)
{
   nir_lower_atomics_callback callback = (nir_lower_atomics_callback)_state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   const nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (get_atomic_op(intr->intrinsic) == nir_num_opcodes)
      return false;

   return callback(intr) != nir_lower_atomics_strategy_none;
}

static nir_intrinsic_instr*
nir_atomic_intrinsic_instr_clone(nir_builder *b,
                                 nir_intrinsic_instr *atomic,
                                 nir_intrinsic_op op)
{
   unsigned bit_size = nir_dest_bit_size(atomic->dest);

   nir_intrinsic_instr *clone = nir_intrinsic_instr_create(b->shader, op);
   nir_ssa_dest_init(&clone->instr, &clone->dest, 1, bit_size, NULL);

   if (nir_intrinsic_has_base(clone)) {
      if (nir_intrinsic_has_base(atomic))
         nir_intrinsic_set_base(clone, nir_intrinsic_base(atomic));
      else
         nir_intrinsic_set_base(clone, 0);
   }

   /* TODO proper alignment */
   if (nir_intrinsic_has_align_mul(clone))
      nir_intrinsic_set_align_mul(clone, bit_size / 8);

   clone->src[0] = atomic->src[0];
   if (op == nir_intrinsic_load_ssbo) {
      clone->src[1] = atomic->src[1];
   }

   return clone;
}

static nir_ssa_def*
lower_atomics_instr_comp_swap(nir_builder *b, nir_intrinsic_instr *atomic)
{
   nir_intrinsic_op load_op = get_atomic_load(atomic->intrinsic);
   nir_intrinsic_op cas_op = get_atomic_cas(atomic->intrinsic);
   unsigned bit_size = nir_dest_bit_size(atomic->dest);
   int s = load_op == nir_intrinsic_load_ssbo ? 2 : 1;
   nir_op op = get_atomic_op(atomic->intrinsic);

   b->cursor = nir_before_instr(&atomic->instr);
   nir_intrinsic_instr *load = nir_atomic_intrinsic_instr_clone(b, atomic, load_op);
   load->num_components = 1;
   nir_builder_instr_insert(b, &load->instr);

   /* TODO two operands */
   nir_ssa_def *operand = nir_ssa_for_src(b, atomic->src[s], 1);

   nir_loop *loop = nir_push_loop(b);

   /* phi for the load/cas dest */
   nir_phi_instr *phi = nir_phi_instr_create(b->shader);
   nir_phi_src *src = ralloc(phi, nir_phi_src);
   src->pred = load->instr.block;
   src->src = nir_src_for_ssa(&load->dest.ssa);
   exec_list_push_tail(&phi->srcs, &src->node);
   nir_ssa_dest_init(&phi->instr, &phi->dest, 1, bit_size, NULL);

   /* do the operation */
   nir_ssa_def *res = nir_build_alu(b, op, &phi->dest.ssa, operand, NULL, NULL);

   /* do the cas */
   nir_intrinsic_instr *cas = nir_atomic_intrinsic_instr_clone(b, atomic, cas_op);
   nir_intrinsic_copy_const_indices(cas, atomic);
   cas->src[s] = nir_src_for_ssa(&phi->dest.ssa);
   cas->src[s + 1] = nir_src_for_ssa(res);
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
   return &cas->dest.ssa;
}

static void
lower_atomics_store_shared(nir_builder *b, nir_ssa_def *offset, nir_ssa_def *value)
{
   nir_intrinsic_instr *init = nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_shared);
   nir_intrinsic_set_base(init, 0);
   nir_intrinsic_set_write_mask(init, 0x1);
   nir_intrinsic_set_align_mul(init, 4);
   init->num_components = 1;
   init->src[0] = nir_src_for_ssa(value);
   init->src[1] = nir_src_for_ssa(offset);
   nir_builder_instr_insert(b, &init->instr);
}

static void
lower_atomics_shared_barrier(nir_builder *b)
{
   nir_intrinsic_instr *mem_barrier = nir_intrinsic_instr_create(b->shader, nir_intrinsic_memory_barrier_shared);
   nir_builder_instr_insert(b, &mem_barrier->instr);
}

static nir_ssa_def*
lower_atomics_instr_spinlock(nir_builder *b, nir_intrinsic_instr *atomic)
{
   nir_intrinsic_op load_op = get_atomic_load(atomic->intrinsic);
   nir_op op = get_atomic_op(atomic->intrinsic);
   assert(load_op == nir_intrinsic_load_shared);

   b->cursor = nir_before_instr(&atomic->instr);

   b->shader->shared_size = align(b->shader->shared_size, 4);
   nir_ssa_def *offset = nir_imm_int(b, b->shader->shared_size);
   b->shader->shared_size += 4;

   /* TODO two operands */
   nir_ssa_def *operand = nir_ssa_for_src(b, atomic->src[1], 1);
   nir_ssa_def *zero = nir_imm_int(b, 0);

   lower_atomics_store_shared(b, offset, zero);
   lower_atomics_shared_barrier(b);

   /* start the loop */
   nir_loop *loop = nir_push_loop(b);

   /* aquire lock */
   nir_intrinsic_instr *lock = nir_intrinsic_instr_create(b->shader, nir_intrinsic_shared_atomic_comp_swap);
   nir_intrinsic_set_base(lock, 0);
   nir_ssa_dest_init(&lock->instr, &lock->dest, 1, 32, NULL);
   lock->src[0] = nir_src_for_ssa(offset);
   lock->src[1] = nir_src_for_ssa(zero);
   lock->src[2] = nir_src_for_ssa(nir_imm_int(b, 1));
   nir_builder_instr_insert(b, &lock->instr);

   /* check if thread owns the lock */
   nir_if *nif = nir_push_if(b, nir_ieq(b, &lock->dest.ssa, zero));

   /* do operation */
   nir_intrinsic_instr *load = nir_atomic_intrinsic_instr_clone(b, atomic, load_op);
   load->num_components = 1;
   nir_builder_instr_insert(b, &load->instr);
   nir_ssa_def *res = nir_build_alu(b, op, &load->dest.ssa, operand, NULL, NULL);
   lower_atomics_store_shared(b, nir_ssa_for_src(b, atomic->src[0], 1), res);

   /* unlock */
   lower_atomics_store_shared(b, offset, zero);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, nif);

   nir_pop_loop(b, loop);

   lower_atomics_shared_barrier(b);

   return &load->dest.ssa;
}

static nir_ssa_def*
lower_atomic_instr(nir_builder *b, nir_instr *insn, void *_state)
{
   nir_intrinsic_instr *atomic = nir_instr_as_intrinsic(insn);
   nir_lower_atomics_callback callback = (nir_lower_atomics_callback)_state;

   nir_lower_atomics_strategy strategy = callback(atomic);

   switch (strategy) {
   case nir_lower_atomics_strategy_comp_swap_loop:
      return lower_atomics_instr_comp_swap(b, atomic);
   case nir_lower_atomics_strategy_spinlock_loop:
      return lower_atomics_instr_spinlock(b, atomic);
   case nir_lower_atomics_strategy_none:
   default:
      return NULL;
   }
}

bool
nir_lower_atomics(nir_shader *shader, nir_lower_atomics_callback callback)
{
   assert(callback);
   return nir_shader_lower_instructions(shader, should_lower_atomic_instr,
                                        lower_atomic_instr, callback);
}
