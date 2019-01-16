#
# Copyright (C) 2019 Red Hat
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

from enum import Enum

# This file defines all gl formats in one place
#
# The Intrinsic class corresponds one-to-one with mesa_gl_format_info
# structure.

class GlFormatType(Enum):
   N = ""
   S = "_SNORM"
   F = "F"
   I = "I"
   U = "UI"

N = GlFormatType.N
S = GlFormatType.S
F = GlFormatType.F
I = GlFormatType.I
U = GlFormatType.U

SWIZZLE_DICT = {'x':0, 'y':1, 'z':2, 'w':3, 's':4}
SWIZZLE_FORMAT_DICT = {
   'R':'x',
   'RG':'xy',
   'RGB':'xyz',
   'RGBA':'xyzw',
   'RGBS':'xyzs',
}

class GlFormat(object):
   """Class that represents all the information about a GL Format.
   NOTE: this must be kept in sync with mesa_gl_format_info.
   """
   def __init__(self, name, type, swizzle, bpc):
      """Parameters:

      - name: the format name
      - type: GlFormatType
      - swizzle: the component swizzle
      - bpc: array of bits per component
      """
      assert isinstance(name, str)
      assert isinstance(type, GlFormatType)
      assert isinstance(swizzle, str)
      assert isinstance(bpc, list)
      assert isinstance(bpc[0], int)

      self.name = name
      self.type = type
      self.swizzle = ''.join(str(SWIZZLE_DICT[e]) for e in swizzle)
      self.bpc = bpc

GLFORMATS = {}

def format(name, type, swizzle, bpc):
   assert name not in GLFORMATS
   GLFORMATS[name] = GlFormat(name, type, swizzle, bpc)
# RGB
for f in ["R", "RG", "RGB", "RGBA"]:
   s = SWIZZLE_FORMAT_DICT[f]
   for type in [N, S, I, U]:
      format(f + "8" + type.value, type, s, [8] * len(s))
   for type in list(GlFormatType):
      format(f + "16" + type.value, type, s, [16] * len(s))
   for type in [F, I, U]:
      format(f + "32" + type.value, type, s, [32] * len(s))

format("RGBA2",          N, "xyzw",  [2, 2, 2, 2])
format("RGB4",           N, "xyz",   [4, 4, 4])
format("RGBA4",          N, "xyzw",  [4, 4, 4, 4])
format("R3_G3_B2",       N, "xyz",   [3, 3, 2])
format("RGB5",           N, "xyz",   [5, 5, 5])
format("RGB5_A1",        N, "xyzw",  [5, 5, 5, 1])
format("RGB565",         N, "xyz",   [5, 6, 5])
format("RGB9_E5",        F, "xyzs" , [9, 9, 9, 0, 5])
format("RGB10",          N, "xyz",   [10, 10, 10])
format("RGB10_A2",       N, "xyzw",  [10, 10, 10, 2])
format("RGB10_A2UI",     U, "xyzw",  [10, 10, 10, 2])
format("R11F_G11F_B10F", F, "xyz",   [11, 11, 10])
format("RGB12",          N, "xyz",   [12, 12, 12])
format("RGBA12",         N, "xyzw",  [12, 12, 12, 12])

# SRGB
format("SRGB8",        N, "xyz",  [8, 8, 8])
format("SRGB8_ALPHA8", N, "xyzw", [8, 8, 8, 8])
