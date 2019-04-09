//
// Copyright 2018 Pierre Moreau
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include "invocation.hpp"

#include "pipe/p_state.h"
#include "util/u_math.h"

#include "core/error.hpp"
#include "util/algorithm.hpp"
#include "util/functional.hpp"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "compiler/spirv/nir_spirv.h"

#include <iostream>

using namespace clover;

#ifdef CLOVER_ALLOW_SPIRV
module
clover::nir::spirv_to_nir(const module &spirv_m,
                          const nir_shader_compiler_options *nir_options) {
   auto msec = find(type_equals(module::section::text_executable), spirv_m.secs);
   module m;
   spirv_to_nir_options spirv_options = {
      .caps = {
         .address = true,
         .float64 = true,
         .int8 = true,
         .int16 = true,
         .int64 = true,
         .kernel = true,
      },
   };

   std::vector<char> text;
   for (const auto &sym : spirv_m.syms) {
      const std::string &name = sym.name;

      nir_function *entry_point =
            spirv_to_nir(
                  ((const uint32_t*)(&(msec.data[0]) + sizeof(struct pipe_llvm_program_header))),
                  ((const struct pipe_llvm_program_header *)&(msec.data[0]))->num_bytes / 4,
                  nullptr,
                  0,
                  MESA_SHADER_KERNEL,
                  name.c_str(),
                  &spirv_options,
                  nir_options, false);

      if (!entry_point)
         continue;

      nir_shader *nir = entry_point->shader;
      nir->info.cs.local_size_variable = true;

      nir_validate_shader(nir, "clover");

      NIR_PASS_V(nir, nir_lower_goto_ifs);
      NIR_PASS_V(nir, nir_opt_dead_cf);

      nir_validate_shader(nir, "clover after structurizing");

      /* calculate input offsets */
      unsigned offset = 0;
      nir_foreach_variable_safe(var, &nir->inputs) {
         offset = align(offset, var->type->cl_alignment());
         var->data.driver_location = offset;
         offset += var->type->cl_size();
      }

      /* inline all functions first */
      NIR_PASS_V(nir, nir_lower_constant_initializers,
                 (nir_variable_mode)(nir_var_function_temp));
      NIR_PASS_V(nir, nir_lower_returns);
      NIR_PASS_V(nir, nir_inline_functions);
      NIR_PASS_V(nir, nir_copy_prop);

      /* Pick off the single entrypoint that we want */
      foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
         if (func != entry_point)
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

      struct blob blob;
      for (const auto &sym : spirv_m.syms) {
         if (sym.name == name) {
               m.syms.emplace_back(name, 0, text.size(), sym.args);
            break;
         }
      }
      assert(!m.syms.empty());

      blob_init(&blob);
      nir_serialize(&blob, nir);
      nir_module nm = {
         .size = blob.size,
      };
      char *start = (char*)&nm.size;
      text.insert(text.end(), start, start + sizeof(nm.size));
      text.insert(text.end(), blob.data, blob.data + blob.size);

      blob_finish(&blob);
      ralloc_free(nir);
   }

   m.secs.emplace_back(0, module::section::text_executable, text.size(), text);
   return m;
}
#else
module
clover::nir::spirv_to_nir(const module &spirv_m,
                          const nir_shader_compiler_options *nir_options) {
   unreachable("opencl-spirv is disable, so we also have no support for nir shaders inside clover");
   throw error(CL_LINKER_NOT_AVAILABLE);
}
#endif
