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

import xml.etree.cElementTree as et

from mako.template import Template

from vk_extensions import *

def _init_exts_from_xml(xml, extensions):
    """ Walk the Vulkan XML and fill out extra extension information. """

    xml = et.parse(xml)

    ext_name_map = {}
    for ext in extensions:
        ext_name_map[ext.name] = ext

    for ext_elem in xml.findall('.extensions/extension'):
        ext_name = ext_elem.attrib['name']
        if ext_name not in ext_name_map:
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

_TEMPLATE_H = Template(COPYRIGHT + """

#ifndef ${name_prefix.upper()}_EXTENSIONS_H
#define ${name_prefix.upper()}_EXTENSIONS_H

#include "stdbool.h"

#define ${name_prefix.upper()}_INSTANCE_EXTENSION_COUNT ${len(instance_extensions)}

extern const VkExtensionProperties ${name_prefix}_instance_extensions[];

struct ${name_prefix}_instance_extension_table {
   union {
      bool extensions[${name_prefix.upper()}_INSTANCE_EXTENSION_COUNT];
      struct {
%for ext in instance_extensions:
         bool ${ext.name[3:]};
%endfor
      };
   };
};

extern const struct ${name_prefix}_instance_extension_table ${name_prefix}_instance_extensions_supported;


#define ${name_prefix.upper()}_DEVICE_EXTENSION_COUNT ${len(device_extensions)}

extern const VkExtensionProperties ${name_prefix}_device_extensions[];

struct ${name_prefix}_device_extension_table {
   union {
      bool extensions[${name_prefix.upper()}_DEVICE_EXTENSION_COUNT];
      struct {
%for ext in device_extensions:
        bool ${ext.name[3:]};
%endfor
      };
   };
};

struct ${name_prefix}_physical_device;

void
${name_prefix}_physical_device_get_supported_extensions(const struct ${name_prefix}_physical_device *device,
                                             struct ${name_prefix}_device_extension_table *extensions);

#endif /* ${name_prefix.upper()}_EXTENSIONS_H */
""")

_TEMPLATE_C = Template(COPYRIGHT + """
#include "${name_prefix}_private.h"

#include "vk_util.h"

/* Convert the VK_USE_PLATFORM_* defines to booleans */
%for platform in ['ANDROID_KHR', 'WAYLAND_KHR', 'XCB_KHR', 'XLIB_KHR', 'DISPLAY_KHR', 'XLIB_XRANDR_EXT']:
#ifdef VK_USE_PLATFORM_${platform}
#   undef VK_USE_PLATFORM_${platform}
#   define VK_USE_PLATFORM_${platform} true
#else
#   define VK_USE_PLATFORM_${platform} false
#endif
%endfor

/* And ANDROID too */
#ifdef ANDROID
#   undef ANDROID
#   define ANDROID true
#else
#   define ANDROID false
#endif

#define ${name_prefix.upper()}_HAS_SURFACE (VK_USE_PLATFORM_WAYLAND_KHR || \\
                         VK_USE_PLATFORM_XCB_KHR || \\
                         VK_USE_PLATFORM_XLIB_KHR || \\
                         VK_USE_PLATFORM_DISPLAY_KHR)

static const uint32_t max_api_version = ${max_api_version.c_vk_version()};

VkResult ${name_prefix}_EnumerateInstanceVersion(
    uint32_t*                                   pApiVersion)
{
    *pApiVersion = max_api_version;
    return VK_SUCCESS;
}

const VkExtensionProperties ${name_prefix}_instance_extensions[${name_prefix.upper()}_INSTANCE_EXTENSION_COUNT] = {
%for ext in instance_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

const struct ${name_prefix}_instance_extension_table ${name_prefix}_instance_extensions_supported = {
%for ext in instance_extensions:
   .${ext.name[3:]} = ${ext.enable},
%endfor
};

uint32_t
${name_prefix}_physical_device_api_version(struct ${name_prefix}_physical_device *device)
{
    uint32_t version = 0;

    uint32_t override = vk_get_version_override();
    if (override)
        return MIN2(override, max_api_version);

%for version in api_versions:
    if (!(${version.enable}))
        return version;
    version = ${version.version.c_vk_version()};

%endfor
    return version;
}

const VkExtensionProperties ${name_prefix}_device_extensions[${name_prefix.upper()}_DEVICE_EXTENSION_COUNT] = {
%for ext in device_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

void
${name_prefix}_physical_device_get_supported_extensions(const struct ${name_prefix}_physical_device *device,
                                             struct ${name_prefix}_device_extension_table *extensions)
{
   *extensions = (struct ${name_prefix}_device_extension_table) {
%for ext in device_extensions:
      .${ext.name[3:]} = ${ext.enable},
%endfor
   };
}
""")

def generate_extensions(max_api_version, api_versions, extensions, name_prefix, xml_files, out_c=None, out_h=None):
    for filename in xml_files:
        _init_exts_from_xml(filename, extensions)

    for ext in extensions:
        assert ext.type == 'instance' or ext.type == 'device'

    template_env = {
        'api_versions': api_versions,
        'max_api_version': max_api_version,
        'name_prefix': name_prefix,
        'instance_extensions': [e for e in extensions if e.type == 'instance'],
        'device_extensions': [e for e in extensions if e.type == 'device'],
    }

    if out_h:
        with open(out_h, 'w') as f:
            f.write(_TEMPLATE_H.render(**template_env))

    if out_c:
        with open(out_c, 'w') as f:
            f.write(_TEMPLATE_C.render(**template_env))
