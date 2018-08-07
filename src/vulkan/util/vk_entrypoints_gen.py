# coding=utf-8
#
# Copyright © 2015, 2017 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#

import math
import os
import xml.etree.cElementTree as et

from collections import OrderedDict, namedtuple
from mako.template import Template

from vk_extensions import *

# We generate a static hash table for entry point lookup
# (vkGetProcAddress). We use a linear congruential generator for our hash
# function and a power-of-two size table. The prime numbers are determined
# experimentally.

TEMPLATE_H = Template("""\
/* This file generated from ${filename}, don't edit directly. */

struct ${name_prefix}_dispatch_table {
   union {
      void *entrypoints[${len(entrypoints)}];
      struct {
      % for e in entrypoints:
        % if e.guard is not None:
#ifdef ${e.guard}
          PFN_${e.name} ${e.name};
#else
          void *${e.name};
# endif
        % else:
          PFN_${e.name} ${e.name};
        % endif
      % endfor
      };
   };
};

%for layer in LAYERS:
extern const struct ${name_prefix}_dispatch_table ${layer}_dispatch_table;
%endfor
extern const struct ${name_prefix}_dispatch_table ${name_prefix}_tramp_dispatch_table;

% for e in entrypoints:
  % if e.alias:
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % for layer in LAYERS:
  ${e.return_type} ${e.prefixed_name(layer)}(${e.decl_params()});
  % endfor
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor
""", output_encoding='utf-8')

TEMPLATE_C = Template(u"""\
/*
 * Copyright © 2015 Intel Corporation
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
 */

/* This file generated from ${filename}, don't edit directly. */

#include "${name_prefix}_private.h"

struct string_map_entry {
   uint32_t name;
   uint32_t hash;
   uint32_t num;
};

/* We use a big string constant to avoid lots of reloctions from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] =
% for s in strmap.sorted_strings:
    "${s.string}\\0"
% endfor
;

static const struct string_map_entry string_map_entries[] = {
% for s in strmap.sorted_strings:
    { ${s.offset}, ${'{:0=#8x}'.format(s.hash)}, ${s.num} }, /* ${s.string} */
% endfor
};

/* Hash table stats:
 * size ${len(strmap.sorted_strings)} entries
 * collisions entries:
% for i in range(10):
 *     ${i}${'+' if i == 9 else ' '}     ${strmap.collisions[i]}
% endfor
 */

#define none 0xffff
static const uint16_t string_map[${strmap.hash_size}] = {
% for e in strmap.mapping:
    ${ '{:0=#6x}'.format(e) if e >= 0 else 'none' },
% endfor
};

int
${name_prefix}_string_map_lookup(const char *str)
{
    static const uint32_t prime_factor = ${strmap.prime_factor};
    static const uint32_t prime_step = ${strmap.prime_step};
    const struct string_map_entry *e;
    uint32_t hash, h;
    uint16_t i;
    const char *p;

    hash = 0;
    for (p = str; *p; p++)
        hash = hash * prime_factor + *p;

    h = hash;
    while (1) {
        i = string_map[h & ${strmap.hash_mask}];
        if (i == none)
           return -1;
        e = &string_map_entries[i];
        if (e->hash == hash && strcmp(str, strings + e->name) == 0)
            return e->num;
        h += prime_step;
    }

    return -1;
}

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */

% for layer in LAYERS:
  % for e in entrypoints:
    % if e.alias:
      <% continue %>
    % endif
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    ${e.return_type} ${e.prefixed_name(layer)}(${e.decl_params()}) __attribute__ ((weak));
    % if e.guard is not None:
#endif // ${e.guard}
    % endif
  % endfor

  const struct ${name_prefix}_dispatch_table ${layer}_dispatch_table = {
  % for e in entrypoints:
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    .${e.name} = ${e.prefixed_name(layer)},
    % if e.guard is not None:
#endif // ${e.guard}
    % endif
  % endfor
  };
% endfor


/** Trampoline entrypoints for all device functions */

% for e in entrypoints:
  % if e.alias or not e.is_device_entrypoint():
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  static ${e.return_type}
  ${e.prefixed_name(name_prefix + '_tramp')}(${e.decl_params()})
  {
    % if e.params[0].type == 'VkDevice':
      ${name_prefix.upper()}_FROM_HANDLE(${name_prefix}_device, ${name_prefix}_device, ${e.params[0].name});
      return ${name_prefix}_device->dispatch.${e.name}(${e.call_params()});
    % elif e.params[0].type == 'VkCommandBuffer':
      ${name_prefix.upper()}_FROM_HANDLE(${name_prefix}_cmd_buffer, ${name_prefix}_cmd_buffer, ${e.params[0].name});
      return ${name_prefix}_cmd_buffer->device->dispatch.${e.name}(${e.call_params()});
    % elif e.params[0].type == 'VkQueue':
      ${name_prefix.upper()}_FROM_HANDLE(${name_prefix}_queue, ${name_prefix}_queue, ${e.params[0].name});
      return ${name_prefix}_queue->device->dispatch.${e.name}(${e.call_params()});
    % else:
      assert(!"Unhandled device child trampoline case: ${e.params[0].type}");
    % endif
  }
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor

const struct ${name_prefix}_dispatch_table ${name_prefix}_tramp_dispatch_table = {
% for e in entrypoints:
  % if not e.is_device_entrypoint():
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
    .${e.name} = ${e.prefixed_name(name_prefix + '_tramp')},
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor
};


/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
bool
${name_prefix}_entrypoint_is_enabled(int index, uint32_t core_version,
                          const struct ${name_prefix}_instance_extension_table *instance,
                          const struct ${name_prefix}_device_extension_table *device)
{
   switch (index) {
% for e in entrypoints:
   case ${e.num}:
      /* ${e.name} */
   % if e.core_version:
      % if e.is_device_entrypoint():
         return ${e.core_version.c_vk_version()} <= core_version;
      % else:
         return !device && ${e.core_version.c_vk_version()} <= core_version;
      % endif
   % elif e.extensions:
     % for ext in e.extensions:
       % if ext.type == 'instance':
      if (!device && instance->${ext.name[3:]}) return true;
        % else:
      if (!device || device->${ext.name[3:]}) return true;
        % endif
     % endfor
      return false;
   % else:
      return true;
   % endif
% endfor
   default:
      return false;
   }
}""", output_encoding='utf-8')

U32_MASK = 2**32 - 1

PRIME_FACTOR = 5024183
PRIME_STEP = 19

class StringIntMapEntry(object):
    def __init__(self, string, num):
        self.string = string
        self.num = num

        # Calculate the same hash value that we will calculate in C.
        h = 0
        for c in string:
            h = ((h * PRIME_FACTOR) + ord(c)) & U32_MASK
        self.hash = h

        self.offset = None

def round_to_pow2(x):
    return 2**int(math.ceil(math.log(x, 2)))

class StringIntMap(object):
    def __init__(self):
        self.baked = False
        self.strings = dict()

    def add_string(self, string, num):
        assert not self.baked
        assert string not in self.strings
        assert num >= 0 and num < 2**31
        self.strings[string] = StringIntMapEntry(string, num)

    def bake(self):
        self.sorted_strings = \
            sorted(self.strings.values(), key=lambda x: x.string)
        offset = 0
        for entry in self.sorted_strings:
            entry.offset = offset
            offset += len(entry.string) + 1

        # Save off some values that we'll need in C
        self.hash_size = round_to_pow2(len(self.strings) * 1.25)
        self.hash_mask = self.hash_size - 1
        self.prime_factor = PRIME_FACTOR
        self.prime_step = PRIME_STEP

        self.mapping = [-1] * self.hash_size
        self.collisions = [0] * 10
        for idx, s in enumerate(self.sorted_strings):
            level = 0
            h = s.hash
            while self.mapping[h & self.hash_mask] >= 0:
                h = h + PRIME_STEP
                level = level + 1
            self.collisions[min(level, 9)] += 1
            self.mapping[h & self.hash_mask] = idx

EntrypointParam = namedtuple('EntrypointParam', 'type name decl')

class EntrypointBase(object):
    def __init__(self, name):
        self.name = name
        self.alias = None
        self.guard = None
        self.enabled = False
        self.num = None
        # Extensions which require this entrypoint
        self.core_version = None
        self.extensions = []

class Entrypoint(EntrypointBase):
    def __init__(self, name, return_type, params, guard = None):
        super(Entrypoint, self).__init__(name)
        self.return_type = return_type
        self.params = params
        self.guard = guard

    def is_device_entrypoint(self):
        return self.params[0].type in ('VkDevice', 'VkCommandBuffer', 'VkQueue')

    def prefixed_name(self, prefix):
        assert self.name.startswith('vk')
        return prefix + '_' + self.name[2:]

    def decl_params(self):
        return ', '.join(p.decl for p in self.params)

    def call_params(self):
        return ', '.join(p.name for p in self.params)

class EntrypointAlias(EntrypointBase):
    def __init__(self, name, entrypoint):
        super(EntrypointAlias, self).__init__(name)
        self.alias = entrypoint

    def is_device_entrypoint(self):
        return self.alias.is_device_entrypoint()

    def prefixed_name(self, prefix):
        return self.alias.prefixed_name(prefix)

def get_entrypoints(doc, entrypoints_to_defines, start_index, max_api_version, extensions):
    """Extract the entry points from the registry."""
    entrypoints = OrderedDict()

    for command in doc.findall('./commands/command'):
        if 'alias' in command.attrib:
            alias = command.attrib['name']
            target = command.attrib['alias']
            entrypoints[alias] = EntrypointAlias(alias, entrypoints[target])
        else:
            name = command.find('./proto/name').text
            ret_type = command.find('./proto/type').text
            params = [EntrypointParam(
                type = p.find('./type').text,
                name = p.find('./name').text,
                decl = ''.join(p.itertext())
            ) for p in command.findall('./param')]
            guard = entrypoints_to_defines.get(name)
            # They really need to be unique
            assert name not in entrypoints
            entrypoints[name] = Entrypoint(name, ret_type, params, guard)

    for feature in doc.findall('./feature'):
        assert feature.attrib['api'] == 'vulkan'
        version = VkVersion(feature.attrib['number'])
        if version > max_api_version:
            continue

        for command in feature.findall('./require/command'):
            e = entrypoints[command.attrib['name']]
            e.enabled = True
            assert e.core_version is None
            e.core_version = version

    supported_exts = dict((ext.name, ext) for ext in extensions)
    for extension in doc.findall('.extensions/extension'):
        ext_name = extension.attrib['name']
        if ext_name not in supported_exts:
            continue

        ext = supported_exts[ext_name]
        ext.type = extension.attrib['type']

        for command in extension.findall('./require/command'):
            e = entrypoints[command.attrib['name']]
            e.enabled = True
            assert e.core_version is None
            e.extensions.append(ext)

    return [e for e in entrypoints.values() if e.enabled]


def get_entrypoints_defines(doc):
    """Maps entry points to extension defines."""
    entrypoints_to_defines = {}

    for extension in doc.findall('./extensions/extension[@platform]'):
        platform = extension.attrib['platform']
        ext = '_KHR'
        if platform.upper() == 'XLIB_XRANDR':
            ext = '_EXT'
        define = 'VK_USE_PLATFORM_' + platform.upper() + ext

        for entrypoint in extension.findall('./require/command'):
            fullname = entrypoint.attrib['name']
            entrypoints_to_defines[fullname] = define

    return entrypoints_to_defines


def generate_entrypoints(max_api_version, extensions, layers, name_prefix, xml_files, out_c, out_h, has_intel_entrypoints=False):
    entrypoints = []

    for filename in xml_files:
        doc = et.parse(filename)
        entrypoints += get_entrypoints(doc, get_entrypoints_defines(doc),
                                       len(entrypoints), max_api_version, extensions)

    if has_intel_entrypoints:
      # Manually add CreateDmaBufImageINTEL for which we don't have an extension
      # defined.
      entrypoints.append(Entrypoint('vkCreateDmaBufImageINTEL', 'VkResult', [
          EntrypointParam('VkDevice', 'device', 'VkDevice device'),
          EntrypointParam('VkDmaBufImageCreateInfo', 'pCreateInfo',
                          'const VkDmaBufImageCreateInfo* pCreateInfo'),
          EntrypointParam('VkAllocationCallbacks', 'pAllocator',
                          'const VkAllocationCallbacks* pAllocator'),
          EntrypointParam('VkDeviceMemory', 'pMem', 'VkDeviceMemory* pMem'),
          EntrypointParam('VkImage', 'pImage', 'VkImage* pImage')
      ]))

    strmap = StringIntMap()
    for num, e in enumerate(entrypoints):
        strmap.add_string(e.name, num)
        e.num = num
    strmap.bake()

    # For outputting entrypoints.h we generate a ${name_prefix}_EntryPoint() prototype
    # per entry point.
    try:
        with open(out_h, 'wb') as f:
            f.write(TEMPLATE_H.render(entrypoints=entrypoints,
                                      LAYERS=layers,
                                      name_prefix=name_prefix,
                                      filename=os.path.basename(__file__)))
        with open(out_c, 'wb') as f:
            f.write(TEMPLATE_C.render(entrypoints=entrypoints,
                                      LAYERS=layers,
                                      name_prefix=name_prefix,
                                      strmap=strmap,
                                      filename=os.path.basename(__file__)))
    except Exception:
        # In the even there's an error this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        if __debug__:
            import sys
            from mako import exceptions
            sys.stderr.write(exceptions.text_error_template().render() + '\n')
            sys.exit(1)
        raise
