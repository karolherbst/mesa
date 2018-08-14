# Copyright © 2017-2018 Intel Corporation

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

nouv_extensions_py = files('nouv_extensions.py')

nouv_entrypoints = custom_target(
  'nouv_entrypoints.[ch]',
  input : ['nouv_entrypoints_gen.py', vk_api_xml],
  output : ['nouv_entrypoints.h', 'nouv_entrypoints.c'],
  command : [
    'env', 'PYTHONPATH=@0@'.format(join_paths(meson.source_root(), 'src/vulkan/util/')),
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--outdir', meson.current_build_dir(),
  ],
  depend_files : [nouv_extensions_py, vk_api_generators_py],
)

nouv_extensions_c = custom_target(
  'nouv_extensions.c',
  input : ['nouv_extensions_gen.py', vk_api_xml],
  output : 'nouv_extensions.c',
  command : [
   'env', 'PYTHONPATH=@0@'.format(join_paths(meson.source_root(), 'src/vulkan/util/')),
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT@',
  ],
  depend_files : [nouv_extensions_py, vk_api_generators_py],
)

nouv_extensions_h = custom_target(
  'nouv_extensions.h',
  input : ['nouv_extensions_gen.py', vk_api_xml],
  output : 'nouv_extensions.h',
  command : [
    'env', 'PYTHONPATH=@0@'.format(join_paths(meson.source_root(), 'src/vulkan/util/')),
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-h', '@OUTPUT@',
  ],
  depend_files : [nouv_extensions_py, vk_api_generators_py],
)

nouv_icd = custom_target(
  'nouv_icd',
  input : 'nouv_icd.py',
  output : 'nouv_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    'env', 'PYTHONPATH=@0@'.format(join_paths(meson.source_root(), 'src/vulkan/util/')),
    prog_python, '@INPUT@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir')),
    '--out', '@OUTPUT@',
  ],
  depend_files : [nouv_extensions_py, vk_api_generators_py],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

nouv_files = files(
  'winsys/nouveau/nouveau_winsys.c',
  'winsys/nouveau/nouveau_winsys_public.h',
  'nouv_device.c',
  'nouv_formats.c',
  'nouv_util.c',
  'nouv_wsi.c',
)

libvulkan_nouv = shared_library(
  'vulkan_nouv',
  [
    nouv_entrypoints,
    nouv_extensions_c,
    nouv_extensions_h,
    nouv_files,
  ],
  include_directories : [
    inc_common,
    inc_vulkan,
    inc_vulkan_util,
    inc_vulkan_wsi,
  ],
  link_whole : [],
  link_with : [
    libmesa_util,
    libvulkan_util,
    libvulkan_wsi,
  ],
  dependencies : [
    dep_libdrm_nouveau,
    dep_thread,
    dep_valgrind,
  ],
  c_args : [
    c_vis_args,
    no_override_init_args,
  ],
  link_args : [
    '-Wl,--build-id=sha1',
    ld_args_bsymbolic,
    ld_args_gc_sections,
  ],
  install : true,
)
