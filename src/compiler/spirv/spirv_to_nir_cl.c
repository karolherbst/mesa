/*
 * Copyright © 2019 Red Hat, Inc.
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
 *    Karol Herbst (kherbst@redhat.com)
 *
 */

#include "util/u_math.h"

#include "nir/nir.h"
#include "spirv/nir_spirv.h"

nir_shader *
spirv_to_nir_cl(const uint32_t *words, size_t word_count,
                const char *entry_point_name,
                const nir_shader_compiler_options *nir_options)
{
   struct spirv_to_nir_options spirv_options = {
      .caps = {
         .address = true,
         .float64 = true,
         .int8 = true,
         .int16 = true,
         .int64 = true,
         .kernel = true,
      },
   };

   nir_shader *nir =
      spirv_to_nir(words, word_count, NULL, 0, MESA_SHADER_KERNEL,
                   entry_point_name, &spirv_options, nir_options, false);

   nir->info.cs.local_size_variable = true;

   nir_validate_shader(nir, "clover");

   NIR_PASS_V(nir, nir_lower_goto_ifs);
   NIR_PASS_V(nir, nir_opt_dead_cf);

   nir_validate_shader(nir, "clover after structurizing");

   /* calculate input offsets */
   unsigned offset = 0;
   nir_foreach_variable_safe(var, &nir->inputs) {
      offset = align(offset, glsl_get_cl_alignment(var->type));
      var->data.driver_location = offset;
      offset += glsl_get_cl_size(var->type);
   }

   /* inline all functions first */
   NIR_PASS_V(nir, nir_lower_constant_initializers,
              (nir_variable_mode)(nir_var_function_temp));
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_copy_prop);

   /* Pick off the single entrypoint that we want */
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);

   nir_validate_shader(nir, "clover after function inlining");

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_find_array_copies);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_split_var_copies);
      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      if (nir_options->lower_all_io_to_temps)
         NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

      NIR_PASS(progress, nir, nir_lower_constant_initializers,
               (nir_variable_mode)(~0));
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_if, false);

      if (nir_options->lower_int64_options)
         NIR_PASS(progress, nir, nir_lower_int64,
                  nir_options->lower_int64_options);
   } while (progress);

   NIR_PASS_V(nir, nir_remove_dead_variables, (nir_variable_mode)(~0));
   NIR_PASS_V(nir, nir_propagate_invariant);

   nir_variable_mode modes = (nir_variable_mode)(
      nir_var_shader_in |
      nir_var_mem_global |
      nir_var_mem_shared);
   nir_address_format format = nir->info.cs.ptr_size == 64 ?
      nir_address_format_64bit_global : nir_address_format_32bit_global;
   NIR_PASS_V(nir, nir_lower_explicit_io, modes, format);

   return nir;
}
