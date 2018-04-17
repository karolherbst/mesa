/*
 * Copyright © 2014 Intel Corporation
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
 *    Connor Abbott (cwabbott0@gmail.com)
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * This lowering pass converts references to input/output variables with
 * loads/stores to actual input/output intrinsics.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

struct lower_io_state {
   void *dead_ctx;
   nir_builder builder;
   const nir_memory_model *mm;
   nir_variable_mode modes;
   nir_lower_io_options options;
   nir_metadata metadata;
};

void
nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                         int (*type_size)(const struct glsl_type *))
{
   unsigned location = 0;

   nir_foreach_variable(var, var_list) {
      /*
       * UBOs have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if ((var->data.mode == nir_var_uniform || var->data.mode == nir_var_shader_storage) &&
          var->interface_type != NULL)
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *size = location;
}

/**
 * Return true if the given variable is a per-vertex input/output array.
 * (such as geometry shader inputs).
 */
bool
nir_is_per_vertex_io(const nir_variable *var, gl_shader_stage stage)
{
   if (var->data.patch || !glsl_type_is_array(var->type))
      return false;

   if (var->data.mode == nir_var_shader_in)
      return stage == MESA_SHADER_GEOMETRY ||
             stage == MESA_SHADER_TESS_CTRL ||
             stage == MESA_SHADER_TESS_EVAL;

   if (var->data.mode == nir_var_shader_out)
      return stage == MESA_SHADER_TESS_CTRL;

   return false;
}

static int
default_type_align(const struct glsl_type *type)
{
   return 1;
}

static inline int
align(int value, int alignment)
{
   return (value + alignment - 1) & ~(alignment - 1);
}

static nir_ssa_def *
get_io_offset(nir_deref_instr *deref, nir_ssa_def **vertex_index,
              struct lower_io_state *state, unsigned *component)
{
   nir_builder *b = &state->builder;
   int (*type_size)(const struct glsl_type *) = state->mm->type_size;
   int (*type_align)(const struct glsl_type *) = state->mm->type_align ?
      state->mm->type_align : default_type_align;
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   assert(path.path[0]->deref_type == nir_deref_type_var ||
          path.path[0]->deref_type == nir_deref_type_cast);
   nir_deref_instr **p = &path.path[1];

   /* For per-vertex input arrays (i.e. geometry shader inputs), keep the
    * outermost array index separate.  Process the rest normally.
    */
   if (vertex_index != NULL) {
      assert((*p)->deref_type == nir_deref_type_array);
      *vertex_index = nir_ssa_for_src(b, (*p)->arr.index, 1);
      p++;
   }

   if (path.path[0]->var->data.compact) {
      assert((*p)->deref_type == nir_deref_type_array);
      assert(glsl_type_is_scalar((*p)->type));

      /* We always lower indirect dereferences for "compact" array vars. */
      nir_const_value *const_index = nir_src_as_const_value((*p)->arr.index);
      assert(const_index);

      const unsigned total_offset = *component + const_index->u32[0];
      const unsigned slot_offset = total_offset / 4;
      *component = total_offset % 4;
      return nir_imm_int(b, type_size(glsl_vec4_type()) * slot_offset);
   }

   /* Just emit code and let constant-folding go to town */
   nir_ssa_def *offset;

   /* for pointer derefs, initial offset is the .x component of src ptr: */
   nir_ssa_def *fptr = NULL;
   if (path.path[0]->deref_type == nir_deref_type_cast) {
      fptr = nir_ssa_for_src(b, path.path[0]->parent, 2);
      offset = nir_channel(b, fptr, 0);
   } else {
      offset = nir_imm_int(b, 0);
   }

   for (; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array ||
          (*p)->deref_type == nir_deref_type_ptr_as_array) {
         unsigned size = type_size((*p)->type);
         nir_ssa_def *index = nir_ssa_for_src(b, (*p)->arr.index, 1);
         nir_ssa_def *mul =
            nir_imul(b, nir_imm_intN_t(b, size, index->bit_size), index);

         offset = nir_iadd(b, offset, nir_u2ptr(b, mul));
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);

         unsigned field_offset = 0;
         for (unsigned i = 0; i < (*p)->strct.index; i++) {
            const struct glsl_type *field_type =
               glsl_get_struct_field(parent->type, i);
            field_offset = align(field_offset, type_align(field_type));
            field_offset += type_size(field_type);
         }
         offset = nir_iadd(b, offset,
                           nir_imm_intN_t(b, field_offset, offset->bit_size));
      } else {
         unreachable("Unsupported deref type");
      }
   }

   /* for pointer deref's, repack the resulting pointer as a fat ptr: */
   if (path.path[0]->deref_type == nir_deref_type_cast)
      offset = nir_vec2(b, offset, nir_channel(b, fptr, 1));

   nir_deref_path_finish(&path);

   return offset;
}

static nir_intrinsic_instr *
build_load(nir_intrinsic_instr *intrin, nir_intrinsic_op op,
           struct lower_io_state *state, nir_ssa_def *src0,
           nir_ssa_def *src1)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader, op);
   load->num_components = intrin->num_components;

   load->src[0] = nir_src_for_ssa(src0);
   if (src1)
      load->src[1] = nir_src_for_ssa(src1);

   return load;
}

static nir_intrinsic_instr *
insert_load(nir_intrinsic_instr *intrin, nir_intrinsic_op op,
            struct lower_io_state *state, nir_ssa_def *src0,
            nir_ssa_def *src1)
{
   static nir_intrinsic_instr *load;

   load = build_load(intrin, op, state, src0, src1);
   nir_ssa_dest_init(&load->instr, &load->dest,
                     intrin->dest.ssa.num_components,
                     intrin->dest.ssa.bit_size, NULL);
   nir_builder_instr_insert(&state->builder, &load->instr);

   return load;
}

static nir_intrinsic_instr *
lower_load(nir_intrinsic_instr *intrin, struct lower_io_state *state,
           nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
           unsigned component)
{
   const nir_shader *nir = state->builder.shader;
   nir_variable_mode mode = var->data.mode;
   nir_ssa_def *barycentric = NULL;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_shader_in:
      if (nir->info.stage == MESA_SHADER_FRAGMENT &&
          nir->options->use_interpolated_input_intrinsics &&
          var->data.interpolation != INTERP_MODE_FLAT) {
         assert(vertex_index == NULL);

         nir_intrinsic_op bary_op;
         if (var->data.sample ||
             (state->options & nir_lower_io_force_sample_interpolation))
            bary_op = nir_intrinsic_load_barycentric_sample;
         else if (var->data.centroid)
            bary_op = nir_intrinsic_load_barycentric_centroid;
         else
            bary_op = nir_intrinsic_load_barycentric_pixel;

         barycentric = nir_load_barycentric(&state->builder, bary_op,
                                            var->data.interpolation);
         op = nir_intrinsic_load_interpolated_input;
      } else {
         op = vertex_index ? nir_intrinsic_load_per_vertex_input :
                             nir_intrinsic_load_input;
      }
      break;
   case nir_var_shader_out:
      op = vertex_index ? nir_intrinsic_load_per_vertex_output :
                          nir_intrinsic_load_output;
      break;
   case nir_var_uniform:
      op = nir_intrinsic_load_uniform;
      break;
   case nir_var_shared:
      op = nir_intrinsic_load_shared;
      break;
   default:
      unreachable("Unknown variable mode");
   }

   nir_intrinsic_instr *load =
      build_load(intrin, op, state,
                 barycentric ? barycentric : vertex_index,
                 offset);

   nir_intrinsic_set_base(load, var->data.driver_location);
   if (mode == nir_var_shader_in || mode == nir_var_shader_out)
      nir_intrinsic_set_component(load, component);

   if (load->intrinsic == nir_intrinsic_load_uniform)
      nir_intrinsic_set_range(load, state->mm->type_size(var->type));

   return load;
}

static nir_intrinsic_instr *
build_store(nir_intrinsic_instr *intrin, nir_intrinsic_op op,
            struct lower_io_state *state, nir_ssa_def *vertex_index,
            nir_ssa_def *offset)
{
   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(state->builder.shader, op);
   store->num_components = intrin->num_components;

   nir_src_copy(&store->src[0], &intrin->src[1], store);

   nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(intrin));

   if (vertex_index)
      store->src[1] = nir_src_for_ssa(vertex_index);

   store->src[vertex_index ? 2 : 1] = nir_src_for_ssa(offset);

   return store;
}

static nir_intrinsic_instr *
lower_store(nir_intrinsic_instr *intrin, struct lower_io_state *state,
            nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
            unsigned component)
{
   nir_variable_mode mode = var->data.mode;

   nir_intrinsic_op op;
   if (mode == nir_var_shared) {
      op = nir_intrinsic_store_shared;
   } else {
      assert(mode == nir_var_shader_out);
      op = vertex_index ? nir_intrinsic_store_per_vertex_output :
                          nir_intrinsic_store_output;
   }

   nir_intrinsic_instr *store =
      build_store(intrin, op, state, vertex_index, offset);

   nir_intrinsic_set_base(store, var->data.driver_location);

   if (mode == nir_var_shader_out)
      nir_intrinsic_set_component(store, component);

   return store;
}

static nir_intrinsic_instr *
lower_atomic(nir_intrinsic_instr *intrin, struct lower_io_state *state,
             nir_variable *var, nir_ssa_def *offset)
{
   assert(var->data.mode == nir_var_shared);

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
#define OP(O) case nir_intrinsic_deref_##O: op = nir_intrinsic_shared_##O; break;
   OP(atomic_exchange)
   OP(atomic_comp_swap)
   OP(atomic_add)
   OP(atomic_imin)
   OP(atomic_umin)
   OP(atomic_imax)
   OP(atomic_umax)
   OP(atomic_and)
   OP(atomic_or)
   OP(atomic_xor)
   OP(atomic_fadd)
   OP(atomic_fmin)
   OP(atomic_fmax)
   OP(atomic_fcomp_swap)
#undef OP
   default:
      unreachable("Invalid atomic");
   }

   nir_intrinsic_instr *atomic =
      nir_intrinsic_instr_create(state->builder.shader, op);

   nir_intrinsic_set_base(atomic, var->data.driver_location);

   atomic->src[0] = nir_src_for_ssa(offset);
   assert(nir_intrinsic_infos[intrin->intrinsic].num_srcs ==
          nir_intrinsic_infos[op].num_srcs);
   for (unsigned i = 1; i < nir_intrinsic_infos[op].num_srcs; i++) {
      nir_src_copy(&atomic->src[i], &intrin->src[i], atomic);
   }

   return atomic;
}

static nir_intrinsic_instr *
lower_interpolate_at(nir_intrinsic_instr *intrin, struct lower_io_state *state,
                     nir_variable *var, nir_ssa_def *offset, unsigned component)
{
   assert(var->data.mode == nir_var_shader_in);

   /* Ignore interpolateAt() for flat variables - flat is flat. */
   if (var->data.interpolation == INTERP_MODE_FLAT)
      return lower_load(intrin, state, NULL, var, offset, component);

   nir_intrinsic_op bary_op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_interp_deref_at_centroid:
      bary_op = (state->options & nir_lower_io_force_sample_interpolation) ?
                nir_intrinsic_load_barycentric_sample :
                nir_intrinsic_load_barycentric_centroid;
      break;
   case nir_intrinsic_interp_deref_at_sample:
      bary_op = nir_intrinsic_load_barycentric_at_sample;
      break;
   case nir_intrinsic_interp_deref_at_offset:
      bary_op = nir_intrinsic_load_barycentric_at_offset;
      break;
   default:
      unreachable("Bogus interpolateAt() intrinsic.");
   }

   nir_intrinsic_instr *bary_setup =
      nir_intrinsic_instr_create(state->builder.shader, bary_op);

   nir_ssa_dest_init(&bary_setup->instr, &bary_setup->dest, 2, 32, NULL);
   nir_intrinsic_set_interp_mode(bary_setup, var->data.interpolation);

   if (intrin->intrinsic == nir_intrinsic_interp_deref_at_sample ||
       intrin->intrinsic == nir_intrinsic_interp_deref_at_offset)
      nir_src_copy(&bary_setup->src[0], &intrin->src[1], bary_setup);

   nir_builder_instr_insert(&state->builder, &bary_setup->instr);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader,
                                 nir_intrinsic_load_interpolated_input);
   load->num_components = intrin->num_components;

   nir_intrinsic_set_base(load, var->data.driver_location);
   nir_intrinsic_set_component(load, component);

   load->src[0] = nir_src_for_ssa(&bary_setup->dest.ssa);
   load->src[1] = nir_src_for_ssa(offset);

   return load;
}

static bool
lower_pointer_deref(nir_intrinsic_instr *intrin, struct lower_io_state *state)
{
   nir_builder *b = &state->builder;
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_ssa_def *fptr, *addr, *atype, *result = NULL;

   if (nir_intrinsic_infos[intrin->intrinsic].has_dest)
      assert(intrin->dest.is_ssa);

   b->cursor = nir_before_instr(&intrin->instr);

   fptr = get_io_offset(deref, NULL, state, NULL);
   addr = nir_channel(b, fptr, 0);     /* fptr.x is address */
   atype = nir_channel(b, fptr, 1);    /* fptr.y is address type */

   /* if ptrsize==64, we can still do the atype comparision in 32b: */
   atype = nir_u2u32(b, atype);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_deref: {
      nir_intrinsic_instr *load;
      nir_ssa_def *g, *l, *p, *c;

      nir_push_if(b, nir_ieq(b, atype, nir_imm_intN_t(b, nir_var_global, atype->bit_size)));
      {
         load = insert_load(intrin, nir_intrinsic_load_global, state, addr, NULL);
         g = &load->dest.ssa;
      }
      nir_push_else(b, NULL);
      {
         nir_push_if(b, nir_ieq(b, atype, nir_imm_intN_t(b, nir_var_shared, atype->bit_size)));
         {
            load = insert_load(intrin, nir_intrinsic_load_shared, state, addr, NULL);
            l = &load->dest.ssa;
         }
         nir_push_else(b, NULL);
         {
            nir_push_if(b, nir_ieq(b, atype, nir_imm_intN_t(b, nir_var_uniform, atype->bit_size)));
            {
               /* for __constant buffers, clover puts the buffer index in the
                * upper 8 bits and the offset in the lower 24 bits.  (Also, it
                * should only be a 32b value.)
                */
               nir_ssa_def *off, *index;

               off   = nir_u2u32(b, addr);
               index = nir_ishr(b, off, nir_imm_int(b, 24));
               off   = nir_iand(b, off, nir_imm_int(b, 0x00ffffff));

               load = insert_load(intrin, nir_intrinsic_load_ubo, state, index, off);
               c = &load->dest.ssa;
            }
            nir_push_else(b, NULL);
            {
               load = insert_load(intrin, nir_intrinsic_load_private, state, addr, NULL);
               p = &load->dest.ssa;
            }
            nir_pop_if(b, NULL);

            result = nir_if_phi(b, c, p);
         }
         nir_pop_if(b, NULL);

         result = nir_if_phi(b, l, result);
      }
      nir_pop_if(b, NULL);

      result = nir_if_phi(b, g, result);

      break;
   }
   case nir_intrinsic_store_deref: {
      nir_intrinsic_instr *store;

      nir_push_if(b, nir_ieq(b, atype, nir_imm_intN_t(b, nir_var_global, atype->bit_size)));
      {
         store = build_store(intrin, nir_intrinsic_store_global, state, NULL, addr);
         nir_builder_instr_insert(b, &store->instr);
      }
      nir_push_else(b, NULL);
      {
         nir_push_if(b, nir_ieq(b, atype, nir_imm_intN_t(b, nir_var_shared, atype->bit_size)));
         {
            store = build_store(intrin, nir_intrinsic_store_shared, state, NULL, addr);
            nir_builder_instr_insert(b, &store->instr);
         }
         nir_push_else(b, NULL);
         {
            store = build_store(intrin, nir_intrinsic_store_private, state, NULL, addr);
            nir_builder_instr_insert(b, &store->instr);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);

      break;
   }
   case nir_intrinsic_address_from_deref: {
      result = fptr;
      break;
   }
   // TODO atomics??
   default:
      unreachable("Unsupported pointer deref type");
      return false;
   }

   if (intrin->intrinsic != nir_intrinsic_address_from_deref)
      state->metadata = nir_metadata_none;

   if (nir_intrinsic_infos[intrin->intrinsic].has_dest)
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(result));

   nir_instr_remove(&intrin->instr);
   nir_deref_instr_remove_if_unused(deref);

   return true;
}

static bool
nir_lower_io_block(nir_block *block,
                   struct lower_io_state *state)
{
   nir_builder *b = &state->builder;
   const nir_shader_compiler_options *options = b->shader->options;
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_store_deref:
      case nir_intrinsic_address_from_deref:
      case nir_intrinsic_deref_atomic_add:
      case nir_intrinsic_deref_atomic_imin:
      case nir_intrinsic_deref_atomic_umin:
      case nir_intrinsic_deref_atomic_imax:
      case nir_intrinsic_deref_atomic_umax:
      case nir_intrinsic_deref_atomic_and:
      case nir_intrinsic_deref_atomic_or:
      case nir_intrinsic_deref_atomic_xor:
      case nir_intrinsic_deref_atomic_exchange:
      case nir_intrinsic_deref_atomic_comp_swap:
      case nir_intrinsic_deref_atomic_fadd:
      case nir_intrinsic_deref_atomic_fmin:
      case nir_intrinsic_deref_atomic_fmax:
      case nir_intrinsic_deref_atomic_fcomp_swap:
         /* We can lower the io for this nir instrinsic */
         break;
      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
         /* We can optionally lower these to load_interpolated_input */
         if (options->use_interpolated_input_intrinsics)
            break;
      default:
         /* We can't lower the io for this nir instrinsic, so skip it */
         continue;
      }

      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

      if (deref->mode == nir_var_pointer) {
         progress |= lower_pointer_deref(intrin, state);
         continue;
      }

      nir_variable *var = nir_deref_instr_get_variable(deref);

      nir_variable_mode mode = var->data.mode;

      if ((state->modes & mode) == 0)
         continue;

      if (mode != nir_var_shader_in &&
          mode != nir_var_shader_out &&
          mode != nir_var_shared &&
          mode != nir_var_uniform)
         continue;

      b->cursor = nir_before_instr(instr);

      const bool per_vertex = nir_is_per_vertex_io(var, b->shader->info.stage);

      nir_ssa_def *offset;
      nir_ssa_def *vertex_index = NULL;
      unsigned component_offset = var->data.location_frac;

      offset = get_io_offset(deref, per_vertex ? &vertex_index : NULL,
                             state, &component_offset);

      nir_intrinsic_instr *replacement;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
         replacement = lower_load(intrin, state, vertex_index, var, offset,
                                  component_offset);
         break;

      case nir_intrinsic_store_deref:
         replacement = lower_store(intrin, state, vertex_index, var, offset,
                                   component_offset);
         break;

      case nir_intrinsic_deref_atomic_add:
      case nir_intrinsic_deref_atomic_imin:
      case nir_intrinsic_deref_atomic_umin:
      case nir_intrinsic_deref_atomic_imax:
      case nir_intrinsic_deref_atomic_umax:
      case nir_intrinsic_deref_atomic_and:
      case nir_intrinsic_deref_atomic_or:
      case nir_intrinsic_deref_atomic_xor:
      case nir_intrinsic_deref_atomic_exchange:
      case nir_intrinsic_deref_atomic_comp_swap:
      case nir_intrinsic_deref_atomic_fadd:
      case nir_intrinsic_deref_atomic_fmin:
      case nir_intrinsic_deref_atomic_fmax:
      case nir_intrinsic_deref_atomic_fcomp_swap:
         assert(vertex_index == NULL);
         replacement = lower_atomic(intrin, state, var, offset);
         break;

      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
         assert(vertex_index == NULL);
         replacement = lower_interpolate_at(intrin, state, var, offset,
                                            component_offset);
         break;

      default:
         continue;
      }

      if (nir_intrinsic_infos[intrin->intrinsic].has_dest) {
         if (intrin->dest.is_ssa) {
            nir_ssa_dest_init(&replacement->instr, &replacement->dest,
                              intrin->dest.ssa.num_components,
                              intrin->dest.ssa.bit_size, NULL);
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                     nir_src_for_ssa(&replacement->dest.ssa));
         } else {
            nir_dest_copy(&replacement->dest, &intrin->dest, &intrin->instr);
         }
      }

      nir_instr_insert_before(&intrin->instr, &replacement->instr);
      nir_instr_remove(&intrin->instr);
      progress = true;
   }

   return progress;
}

static bool
nir_lower_io_impl(nir_function_impl *impl, nir_variable_mode modes,
                  const nir_memory_model *mm, nir_lower_io_options options)
{
   struct lower_io_state state;
   bool progress = false;

   nir_builder_init(&state.builder, impl);
   state.dead_ctx = ralloc_context(NULL);
   state.modes = modes;
   state.mm = mm;
   state.options = options;
   state.metadata = nir_metadata_block_index |
                    nir_metadata_dominance;

   nir_foreach_block(block, impl) {
      progress |= nir_lower_io_block(block, &state);
   }

   ralloc_free(state.dead_ctx);

   nir_metadata_preserve(impl, state.metadata);
   return progress;
}

bool
nir_lower_io2(nir_shader *shader, nir_variable_mode modes,
             const nir_memory_model *mm, nir_lower_io_options options)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         progress |= nir_lower_io_impl(function->impl, modes,
                                       mm, options);
      }
   }

   /* calculate position for entrypoint parameters (for CL style
    * kernel shaders)
    */
   if (modes & nir_var_param) {
      nir_function *function =
         nir_shader_get_entrypoint(shader)->function;
      unsigned offset = 0;
      for (unsigned i = 0; i < function->num_params; i++) {
         nir_variable *var = function->params[i].var;
         offset = align(offset, mm->type_align(var->type));
         var->data.driver_location = offset;
         offset += mm->type_size(var->type);
      }
   }

   return progress;
}


bool
nir_lower_io(nir_shader *shader, nir_variable_mode modes,
             int (*type_size)(const struct glsl_type *),
             nir_lower_io_options options)
{
   nir_memory_model mm = {
         .type_size = type_size,
   };
   return nir_lower_io2(shader, modes, &mm, options);
}

/**
 * Return the offset source for a load/store intrinsic.
 */
nir_src *
nir_get_io_offset_src(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_uniform:
      return &instr->src[0];
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_store_output:
      return &instr->src[1];
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_per_vertex_output:
      return &instr->src[2];
   default:
      return NULL;
   }
}

/**
 * Return the vertex index source for a load/store per_vertex intrinsic.
 */
nir_src *
nir_get_io_vertex_index_src(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
      return &instr->src[0];
   case nir_intrinsic_store_per_vertex_output:
      return &instr->src[1];
   default:
      return NULL;
   }
}
