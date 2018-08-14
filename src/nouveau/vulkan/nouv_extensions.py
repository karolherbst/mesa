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

API_PATCH_VERSION = 0

# Supported API versions.  Each one is the maximum patch version for the given
# version.  Version come in increasing order and each version is available if
# it's provided "enable" condition is true and all previous versions are
# available.
API_VERSIONS = [
    ApiVersion('1.0.83',   True),
]

MAX_API_VERSION = None # Computed later

# On Android, we disable all surface and swapchain extensions. Android's Vulkan
# loader implements VK_KHR_surface and VK_KHR_swapchain, and applications
# cannot access the driver's implementation. Moreoever, if the driver exposes
# the those extension strings, then tests dEQP-VK.api.info.instance.extensions
# and dEQP-VK.api.info.device fail due to the duplicated strings.
EXTENSIONS = [
]

MAX_API_VERSION = VkVersion('0.0.0')
for version in API_VERSIONS:
    version.version = VkVersion(version.version)
    version.version.patch = API_PATCH_VERSION
    assert version.version > MAX_API_VERSION
    MAX_API_VERSION = version.version

