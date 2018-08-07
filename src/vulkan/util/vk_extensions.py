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

import argparse
import copy
import re
import xml.etree.cElementTree as et

def _bool_to_c_expr(b):
    if b is True:
        return 'true'
    elif b is False:
        return 'false'
    else:
        return b

class Extension:
    def __init__(self, name, ext_version, enable):
        self.name = name
        self.ext_version = int(ext_version)
        self.enable = _bool_to_c_expr(enable)

class ApiVersion:
    def __init__(self, version, enable):
        self.version = version
        self.enable = _bool_to_c_expr(enable)


class VkVersion:
    def __init__(self, string):
        split = string.split('.')
        self.major = int(split[0])
        self.minor = int(split[1])
        if len(split) > 2:
            assert len(split) == 3
            self.patch = int(split[2])
        else:
            self.patch = None

        # Sanity check.  The range bits are required by the definition of the
        # VK_MAKE_VERSION macro
        assert self.major < 1024 and self.minor < 1024
        assert self.patch is None or self.patch < 4096
        assert(str(self) == string)

    def __str__(self):
        ver_list = [str(self.major), str(self.minor)]
        if self.patch is not None:
            ver_list.append(str(self.patch))
        return '.'.join(ver_list)

    def c_vk_version(self):
        patch = self.patch if self.patch is not None else 0
        ver_list = [str(self.major), str(self.minor), str(patch)]
        return 'VK_MAKE_VERSION(' + ', '.join(ver_list) + ')'

    def __int_ver(self):
        # This is just an expansion of VK_VERSION
        patch = self.patch if self.patch is not None else 0
        return (self.major << 22) | (self.minor << 12) | patch

    def __cmp__(self, other):
        # If only one of them has a patch version, "ignore" it by making
        # other's patch version match self.
        if (self.patch is None) != (other.patch is None):
            other = copy.copy(other)
            other.patch = self.patch

        return self.__int_ver().__cmp__(other.__int_ver())
