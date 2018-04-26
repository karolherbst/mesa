from __future__ import print_function

template = """\
/* Copyright (C) 2014 Connor Abbott
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
 *    Connor Abbott (cwabbott0@gmail.com)
 */

#ifndef _NIR_OPCODES_
#define _NIR_OPCODES_

<% opcode_names = sorted(opcodes.keys()) %>

typedef enum {
% for name in opcode_names:
   nir_op_${name},
% endfor
   nir_last_opcode = nir_op_${opcode_names[-1]},
   nir_num_opcodes = nir_last_opcode + 1
} nir_op;

% for mode in [('rtne', '_rtne'), ('rtz', '_rtz'), ('ru', '_ru'), ('rd', '_rd'), ('undef', '')]:
static inline bool
nir_cvt_is_${mode[0]}(nir_op op)
{
   switch (op) {
% for src_type in ['f', 'i', 'u']:
%    if src_type == 'f':
<%      dst_types = ['i', 'u', 'f'] %>
%    else:
<%      dst_types = ['f'] %>
%    endif
%    for dst_type in dst_types:
%       if dst_type == 'f':
<%         bit_sizes = [16, 32, 64] %>
<%         sat_modes = [''] %>
%       else:
<%         bit_sizes = [8, 16, 32, 64] %>
<%         sat_modes = ['_sat', ''] %>
%       endif
%       for bit_size in bit_sizes:
%          for sat_mode in sat_modes:
   case nir_op_${src_type}2${dst_type}${bit_size}${mode[1]}${sat_mode}:
%          endfor
%       endfor
%    endfor
% endfor
      return true;
   default:
      return false;
   }
}
% endfor

static inline bool
nir_cvt_is_sat(nir_op op)
{
   switch (op) {
%  for dst_type in ['i', 'u']:
%     for src_type in ['i', 'u', 'f']:
%     if src_type == 'f':
<%       rnd_modes = ['_rtne', '_rtz', '_ru', '_rd', ''] %>
%     else:
<%       rnd_modes = [''] %>
%     endif
%        for bit_size in [8, 16, 32, 64]:
%           for rnd_mode in rnd_modes:
   case nir_op_${src_type}2${dst_type}${bit_size}${rnd_mode}_sat:
%           endfor
%        endfor
%     endfor
%  endfor
      return true;
   default:
      return false;
   }
}

#endif /* _NIR_OPCODES_ */"""

from nir_opcodes import opcodes
from mako.template import Template

print(Template(template).render(opcodes=opcodes))
