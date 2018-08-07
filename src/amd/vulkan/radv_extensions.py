COPYRIGHT = """\
/*
 * Copyright 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
"""

from vk_extensions import ApiVersion, Extension, VkVersion

API_PATCH_VERSION = 80

# Supported API versions.  Each one is the maximum patch version for the given
# version.  Version come in increasing order and each version is available if
# it's provided "enable" condition is true and all previous versions are
# available.
API_VERSIONS = [
    ApiVersion('1.0',   True),
    ApiVersion('1.1',   '!ANDROID && device->rad_info.has_syncobj_wait_for_submit'),
]

MAX_API_VERSION = None # Computed later

# On Android, we disable all surface and swapchain extensions. Android's Vulkan
# loader implements VK_KHR_surface and VK_KHR_swapchain, and applications
# cannot access the driver's implementation. Moreoever, if the driver exposes
# the those extension strings, then tests dEQP-VK.api.info.instance.extensions
# and dEQP-VK.api.info.device fail due to the duplicated strings.
EXTENSIONS = [
    Extension('VK_ANDROID_native_buffer',                 5, 'ANDROID && device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_16bit_storage',                     1, 'HAVE_LLVM >= 0x0700'),
    Extension('VK_KHR_bind_memory2',                      1, True),
    Extension('VK_KHR_create_renderpass2',                1, True),
    Extension('VK_KHR_dedicated_allocation',              1, True),
    Extension('VK_KHR_descriptor_update_template',        1, True),
    Extension('VK_KHR_device_group',                      1, True),
    Extension('VK_KHR_device_group_creation',             1, True),
    Extension('VK_KHR_draw_indirect_count',               1, True),
    Extension('VK_KHR_external_fence',                    1, 'device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_external_fence_capabilities',       1, True),
    Extension('VK_KHR_external_fence_fd',                 1, 'device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_external_memory',                   1, True),
    Extension('VK_KHR_external_memory_capabilities',      1, True),
    Extension('VK_KHR_external_memory_fd',                1, True),
    Extension('VK_KHR_external_semaphore',                1, 'device->rad_info.has_syncobj'),
    Extension('VK_KHR_external_semaphore_capabilities',   1, True),
    Extension('VK_KHR_external_semaphore_fd',             1, 'device->rad_info.has_syncobj'),
    Extension('VK_KHR_get_display_properties2',           1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_KHR_get_memory_requirements2',          1, True),
    Extension('VK_KHR_get_physical_device_properties2',   1, True),
    Extension('VK_KHR_get_surface_capabilities2',         1, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_image_format_list',                 1, True),
    Extension('VK_KHR_incremental_present',               1, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_maintenance1',                      1, True),
    Extension('VK_KHR_maintenance2',                      1, True),
    Extension('VK_KHR_maintenance3',                      1, True),
    Extension('VK_KHR_push_descriptor',                   1, True),
    Extension('VK_KHR_relaxed_block_layout',              1, True),
    Extension('VK_KHR_sampler_mirror_clamp_to_edge',      1, True),
    Extension('VK_KHR_shader_draw_parameters',            1, True),
    Extension('VK_KHR_storage_buffer_storage_class',      1, True),
    Extension('VK_KHR_surface',                          25, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_swapchain',                        68, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_variable_pointers',                 1, True),
    Extension('VK_KHR_wayland_surface',                   6, 'VK_USE_PLATFORM_WAYLAND_KHR'),
    Extension('VK_KHR_xcb_surface',                       6, 'VK_USE_PLATFORM_XCB_KHR'),
    Extension('VK_KHR_xlib_surface',                      6, 'VK_USE_PLATFORM_XLIB_KHR'),
    Extension('VK_KHR_multiview',                         1, True),
    Extension('VK_KHR_display',                          23, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_direct_mode_display',               1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_acquire_xlib_display',              1, 'VK_USE_PLATFORM_XLIB_XRANDR_EXT'),
    Extension('VK_EXT_conditional_rendering',             1, True),
    Extension('VK_EXT_display_surface_counter',           1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_display_control',                   1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_debug_report',                      9, True),
    Extension('VK_EXT_depth_range_unrestricted',          1, True),
    Extension('VK_EXT_descriptor_indexing',               2, True),
    Extension('VK_EXT_discard_rectangles',                1, True),
    Extension('VK_EXT_external_memory_dma_buf',           1, True),
    Extension('VK_EXT_external_memory_host',              1, 'device->rad_info.has_userptr'),
    Extension('VK_EXT_global_priority',                   1, 'device->rad_info.has_ctx_priority'),
    Extension('VK_EXT_sampler_filter_minmax',             1, 'device->rad_info.chip_class >= CIK'),
    Extension('VK_EXT_shader_viewport_index_layer',       1, True),
    Extension('VK_EXT_shader_stencil_export',             1, True),
    Extension('VK_EXT_vertex_attribute_divisor',          1, True),
    Extension('VK_AMD_draw_indirect_count',               1, True),
    Extension('VK_AMD_gcn_shader',                        1, True),
    Extension('VK_AMD_rasterization_order',               1, 'device->has_out_of_order_rast'),
    Extension('VK_AMD_shader_core_properties',            1, True),
    Extension('VK_AMD_shader_info',                       1, True),
    Extension('VK_AMD_shader_trinary_minmax',             1, True),
]

MAX_API_VERSION = VkVersion('0.0.0')
for version in API_VERSIONS:
    version.version = VkVersion(version.version)
    version.version.patch = API_PATCH_VERSION
    assert version.version > MAX_API_VERSION
    MAX_API_VERSION = version.version
