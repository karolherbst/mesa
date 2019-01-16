
template = """\
/* Copyright (C) 2019 Red Hat
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

#include <stddef.h>

#include "glformats_info.h"

#include "util/macros.h"

static const struct mesa_gl_format_info mesa_gl_format_infos[${len(GLFORMATS)}] = {
% for name, format in GLFORMATS:
{
   .name = "GL_${name}",
   .format = GL_${name},
   .num_components = ${len(format.swizzle)},
   .swizzle = {
      ${", ".join(str(v) for v in format.swizzle)},
   },
   .bits = {
      ${", ".join(str(v) for v in format.bpc)},
   },
},
% endfor
};

const struct mesa_gl_format_info *
_mesa_gl_format_get_info_for_format(GLenum format)
{
   const struct mesa_gl_format_info *info;
   switch (format) {
% for idx, format in enumerate(GLFORMATS):
   case GL_${format[0]}:
      info = &mesa_gl_format_infos[${idx}];
      break;
% endfor
   default:
      return NULL;
   };
   assert(info->format == format);
   return info;
}

"""

from glformats import GLFORMATS
from mako.template import Template
import argparse
import os

def main():
    print(Template(template, output_encoding='utf-8').render_unicode(GLFORMATS=sorted(GLFORMATS.items())))

if __name__ == '__main__':
    main()
