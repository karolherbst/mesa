# Copyright © 

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

rust = import('unstable-rust')

libmesa_rust_util_files = files(
  'util/lib.rs',
  'util/properties.rs',
  'util/ptr.rs',
  'util/string.rs',
)

libmesa_rust_files = files(
  'mesa/lib.rs',
  'mesa/pipe/device.rs',
  'mesa/pipe/mod.rs',
  'mesa/pipe/screen.rs',
)

rusticl_files = files(
  'lib.rs',
  'api/bindings.rs',
  'api/device.rs',
  'api/icd.rs',
  'api/mod.rs',
  'api/platform.rs',
  'api/util.rs',
  'core/device.rs',
  'core/mod.rs',
)

rusticl_bindgen_args = [
  '--no-convert-floats',
  '--use-array-pointers-in-arguments',
  '--size_t-is-usize',
  '--default-enum-style', 'rust',
]

rusticl_opencl_bindings_rs = rust.bindgen(
  input : [
    'rusticle_opencl_bindings.h',
    opencl_headers,
  ],
  output : 'rusticl_opencl_bindings.rs',
  include_directories : [
    inc_include,
  ],
  c_args : [
    '-DCL_USE_DEPRECATED_OPENCL_1_0_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_1_1_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_1_2_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_2_0_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_2_1_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_2_2_APIS',
    '-DCL_TARGET_OPENCL_VERSION=300',
  ],
  args : [
    rusticl_bindgen_args,
    '--disable-header-comment',
    '--ignore-functions',
    '--raw-line', 'use crate::api::context::_cl_context;',
    '--raw-line', 'use crate::api::device::_cl_device_id;',
    '--raw-line', 'use crate::api::platform::_cl_platform_id;',
    '--raw-line', 'pub type cl_context = *mut _cl_context;',
    '--raw-line', 'pub type cl_device_id = *const _cl_device_id;',
    '--raw-line', 'pub type cl_platform_id = *const _cl_platform_id;',
    '--whitelist-type', 'cl_.*',
    '--blacklist-type', 'cl_platform_id',
    '--blacklist-type', '_cl_platform_id',
    '--blacklist-type', 'cl_device_id',
    '--blacklist-type', '_cl_device_id',
    '--blacklist-type', 'cl_context',
    '--blacklist-type', '_cl_context',
    '--whitelist-var', 'CL_.*',
  ],
)

rusticl_mesa_bindings_rs = rust.bindgen(
  input : 'rusticl_mesa_bindings.h',
  output : 'rusticl_mesa_bindings.rs',
  include_directories : [
    inc_gallium,
    inc_gallium_aux,
    inc_include,
    inc_src,
  ],
  c_args : pre_args,
  args : [
    rusticl_bindgen_args,
    '--whitelist-function', 'pipe_loader_.*',
    '--whitelist-type', 'pipe_endian',
  ],
)

idep_rusticl_gen = declare_dependency(
  sources: [
    rusticl_opencl_bindings_rs,
  ],
)

libmesa_rust_gen = static_library(
  'mesa_rust_gen',
  rusticl_mesa_bindings_rs,
  gnu_symbol_visibility : 'hidden',
  link_with: [
    libgallium,
    libpipe_loader_dynamic,
  ],
  rust_crate_type : 'rlib',
  rust_args : [
    '-Anon_snake_case',
    '-Anon_camel_case_types',
  ]
)

libmesa_rust_util = static_library(
  'mesa_rust_util',
  [libmesa_rust_util_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'rlib',
)

libmesa_rust = static_library(
  'mesa_rust',
  [libmesa_rust_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'rlib',
  link_with : [
    libmesa_rust_gen,
    libmesa_rust_util,
  ]
)

librusticl = static_library(
  'rusticl',
  [rusticl_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'staticlib',
  link_with : [
    libmesa_rust,
  ],
)
