/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 *
 */

#include <xf86drm.h>
#include <nouveau_drm.h>
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_screen.h"

#include "nv_object.xml.h"
#include "nv_m2mf.xml.h"
#include "nv30/nv30-40_3d.xml.h"
#include "nv30/nv01_2d.xml.h"

#include "nouveau_fence.h"
#include "nv30/nv30_screen.h"
#include "nv30/nv30_context.h"
#include "nv30/nv30_resource.h"
#include "nv30/nv30_format.h"

#define RANKINE_0397_CHIPSET 0x00000003
#define RANKINE_0497_CHIPSET 0x000001e0
#define RANKINE_0697_CHIPSET 0x00000010
#define CURIE_4097_CHIPSET   0x00000baf
#define CURIE_4497_CHIPSET   0x00005450
#define CURIE_4497_CHIPSET6X 0x00000088

static int
nv30_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct nv30_screen *screen = nv30_screen(pscreen);
   unsigned eng3d_oclass = screen->eng3d_oclass;
   struct nouveau_device *dev = nouveau_screen(pscreen)->device;

   switch (param) {
   /* non-boolean capabilities */
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return (eng3d_oclass >= NV40_3D_CLASS) ? 4 : 1;
   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
      return 13;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 10;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 13;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 120;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 16;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return NOUVEAU_MIN_BUFFER_MAP_ALIGN;
   case PIPE_CAP_MAX_VIEWPORTS:
      return 1;
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   /* supported capabilities */
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
   case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
      return 1;
   /* nv35 capabilities */
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
      return eng3d_oclass == NV35_3D_CLASS || eng3d_oclass >= NV40_3D_CLASS;
   /* nv4x capabilities */
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
   case PIPE_CAP_PRIMITIVE_RESTART:
      return (eng3d_oclass >= NV40_3D_CLASS) ? 1 : 0;
   /* unsupported */
   case PIPE_CAP_DEPTH_CLIP_DISABLE_SEPARATE:
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
   case PIPE_CAP_SM3:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR: /* XXX: yes? */
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
   case PIPE_CAP_MIN_TEXEL_OFFSET:
   case PIPE_CAP_MAX_TEXEL_OFFSET:
   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
   case PIPE_CAP_MAX_VERTEX_STREAMS:
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_START_INSTANCE:
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
   case PIPE_CAP_TEXTURE_GATHER_SM5:
   case PIPE_CAP_FAKE_SW_MSAA:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
   case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_COMPUTE:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_VERTEXID_NOBASE:
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
   case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_SHAREABLE_SHADERS:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_DRAW_PARAMETERS:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_GENERATE_MIPMAP:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
   case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
   case PIPE_CAP_QUERY_MEMORY_INFO:
   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
   case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
   case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
   case PIPE_CAP_CULL_DISTANCE:
   case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
   case PIPE_CAP_TGSI_VOTE:
   case PIPE_CAP_MAX_WINDOW_RECTANGLES:
   case PIPE_CAP_POLYGON_OFFSET_UNITS_UNSCALED:
   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
   case PIPE_CAP_TGSI_CAN_READ_OUTPUTS:
   case PIPE_CAP_NATIVE_FENCE_FD:
   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
   case PIPE_CAP_TGSI_FS_FBFETCH:
   case PIPE_CAP_TGSI_MUL_ZERO_WINS:
   case PIPE_CAP_DOUBLES:
   case PIPE_CAP_INT64:
   case PIPE_CAP_INT64_DIVMOD:
   case PIPE_CAP_TGSI_TEX_TXF_LZ:
   case PIPE_CAP_TGSI_CLOCK:
   case PIPE_CAP_POLYGON_MODE_FILL_RECTANGLE:
   case PIPE_CAP_SPARSE_BUFFER_PAGE_SIZE:
   case PIPE_CAP_TGSI_BALLOT:
   case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
   case PIPE_CAP_CAN_BIND_CONST_BUFFER_AS_VERTEX:
   case PIPE_CAP_POST_DEPTH_COVERAGE:
   case PIPE_CAP_BINDLESS_TEXTURE:
   case PIPE_CAP_NIR_SAMPLERS_AS_DEREF:
   case PIPE_CAP_QUERY_SO_OVERFLOW:
   case PIPE_CAP_MEMOBJ:
   case PIPE_CAP_LOAD_CONSTBUF:
   case PIPE_CAP_TGSI_ANY_REG_AS_ADDRESS:
   case PIPE_CAP_TILE_RASTER_ORDER:
   case PIPE_CAP_MAX_COMBINED_SHADER_OUTPUT_RESOURCES:
   case PIPE_CAP_FRAMEBUFFER_MSAA_CONSTRAINTS:
   case PIPE_CAP_SIGNED_VERTEX_BUFFER_OFFSET:
   case PIPE_CAP_CONTEXT_PRIORITY_MASK:
   case PIPE_CAP_FENCE_SIGNAL:
   case PIPE_CAP_CONSTBUF0_FLAGS:
   case PIPE_CAP_PACKED_UNIFORMS:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_TRIANGLES:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_POINTS_LINES:
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_TRIANGLES:
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_POINTS_LINES:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_DEPTH_COVERAGE:
   case PIPE_CAP_MAX_CONSERVATIVE_RASTER_SUBPIXEL_PRECISION_BIAS:
   case PIPE_CAP_PROGRAMMABLE_SAMPLE_LOCATIONS:
   case PIPE_CAP_MAX_TEXTURE_UPLOAD_MEMORY_BUDGET:
      return 0;

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 32;
   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 1 << 27;
   case PIPE_CAP_VENDOR_ID:
      return 0x10de;
   case PIPE_CAP_DEVICE_ID: {
      uint64_t device_id;
      if (nouveau_getparam(dev, NOUVEAU_GETPARAM_PCI_DEVICE, &device_id)) {
         NOUVEAU_ERR("NOUVEAU_GETPARAM_PCI_DEVICE failed.\n");
         return -1;
      }
      return device_id;
   }
   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY:
      return dev->vram_size >> 20;
   case PIPE_CAP_UMA:
      return 0;
   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
nv30_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   struct nv30_screen *screen = nv30_screen(pscreen);
   unsigned eng3d_oclass = screen->eng3d_oclass;

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 10.0;
   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 64.0;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return (eng3d_oclass >= NV40_3D_CLASS) ? 16.0 : 8.0;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 15.0;
   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0;
   default:
      debug_printf("unknown paramf %d\n", param);
      return 0;
   }
}

static int
nv30_screen_get_shader_param(struct pipe_screen *pscreen,
                             enum pipe_shader_type shader,
                             enum pipe_shader_cap param)
{
   struct nv30_screen *screen = nv30_screen(pscreen);
   unsigned eng3d_oclass = screen->eng3d_oclass;

   switch (shader) {
   case PIPE_SHADER_VERTEX:
      switch (param) {
      case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
         return (eng3d_oclass >= NV40_3D_CLASS) ? 512 : 256;
      case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
         return (eng3d_oclass >= NV40_3D_CLASS) ? 512 : 0;
      case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
         return 0;
      case PIPE_SHADER_CAP_MAX_INPUTS:
      case PIPE_SHADER_CAP_MAX_OUTPUTS:
         return 16;
      case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
         return ((eng3d_oclass >= NV40_3D_CLASS) ? (468 - 6): (256 - 6)) * sizeof(float[4]);
      case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
         return 1;
      case PIPE_SHADER_CAP_MAX_TEMPS:
         return (eng3d_oclass >= NV40_3D_CLASS) ? 32 : 13;
      case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
         return 32;
      case PIPE_SHADER_CAP_PREFERRED_IR:
         return PIPE_SHADER_IR_TGSI;
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         return 0;
      case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      case PIPE_SHADER_CAP_SUBROUTINES:
      case PIPE_SHADER_CAP_INTEGERS:
      case PIPE_SHADER_CAP_INT64_ATOMICS:
      case PIPE_SHADER_CAP_FP16:
      case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
      case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
      case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
      case PIPE_SHADER_CAP_SCALAR_ISA:
         return 0;
      default:
         debug_printf("unknown vertex shader param %d\n", param);
         return 0;
      }
      break;
   case PIPE_SHADER_FRAGMENT:
      switch (param) {
      case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
         return 4096;
      case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
         return 0;
      case PIPE_SHADER_CAP_MAX_INPUTS:
         return 8; /* should be possible to do 10 with nv4x */
      case PIPE_SHADER_CAP_MAX_OUTPUTS:
         return 4;
      case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
         return ((eng3d_oclass >= NV40_3D_CLASS) ? 224 : 32) * sizeof(float[4]);
      case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
         return 1;
      case PIPE_SHADER_CAP_MAX_TEMPS:
         return 32;
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         return 16;
      case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
         return 32;
      case PIPE_SHADER_CAP_PREFERRED_IR:
         return PIPE_SHADER_IR_TGSI;
      case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      case PIPE_SHADER_CAP_SUBROUTINES:
      case PIPE_SHADER_CAP_INTEGERS:
      case PIPE_SHADER_CAP_FP16:
      case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
      case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
      case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
      case PIPE_SHADER_CAP_SCALAR_ISA:
         return 0;
      default:
         debug_printf("unknown fragment shader param %d\n", param);
         return 0;
      }
      break;
   default:
      return 0;
   }
}

static boolean
nv30_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned bindings)
{
   if (sample_count > nv30_screen(pscreen)->max_sample_count)
      return false;

   if (!(0x00000017 & (1 << sample_count)))
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* shared is always supported */
   bindings &= ~PIPE_BIND_SHARED;

   return (nv30_format_info(pscreen, format)->bindings & bindings) == bindings;
}

static void
nv30_screen_destroy(struct pipe_screen *pscreen)
{
   struct nv30_screen *screen = nv30_screen(pscreen);

   if (!nouveau_drm_screen_unref(&screen->base))
      return;

   nouveau_screen_fini(&screen->base);
   FREE(screen);
}

#define FAIL_SCREEN_INIT(str, err)                    \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      screen->base.base.context_create = NULL;        \
      return &screen->base;                           \
   } while(0)

struct nouveau_screen *
nv30_screen_create(struct nouveau_device *dev)
{
   struct nv30_screen *screen;
   struct pipe_screen *pscreen;
   unsigned oclass = 0;
   int ret;

   switch (dev->chipset & 0xf0) {
   case 0x30:
      if (RANKINE_0397_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV30_3D_CLASS;
      else
      if (RANKINE_0697_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV34_3D_CLASS;
      else
      if (RANKINE_0497_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV35_3D_CLASS;
      break;
   case 0x40:
      if (CURIE_4097_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV40_3D_CLASS;
      else
      if (CURIE_4497_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV44_3D_CLASS;
      break;
   case 0x60:
      if (CURIE_4497_CHIPSET6X & (1 << (dev->chipset & 0x0f)))
         oclass = NV44_3D_CLASS;
      break;
   default:
      break;
   }

   if (!oclass) {
      NOUVEAU_ERR("unknown 3d class for 0x%02x\n", dev->chipset);
      return NULL;
   }

   screen = CALLOC_STRUCT(nv30_screen);
   if (!screen)
      return NULL;

   screen->eng3d_oclass = oclass;
   pscreen = &screen->base.base;
   pscreen->destroy = nv30_screen_destroy;

   /*
    * Some modern apps try to use msaa without keeping in mind the
    * restrictions on videomem of older cards. Resulting in dmesg saying:
    * [ 1197.850642] nouveau E[soffice.bin[3785]] fail ttm_validate
    * [ 1197.850648] nouveau E[soffice.bin[3785]] validating bo list
    * [ 1197.850654] nouveau E[soffice.bin[3785]] validate: -12
    *
    * Because we are running out of video memory, after which the program
    * using the msaa visual freezes, and eventually the entire system freezes.
    *
    * To work around this we do not allow msaa visauls by default and allow
    * the user to override this via NV30_MAX_MSAA.
    */
   screen->max_sample_count = debug_get_num_option("NV30_MAX_MSAA", 0);
   if (screen->max_sample_count > 4)
      screen->max_sample_count = 4;

   pscreen->get_param = nv30_screen_get_param;
   pscreen->get_paramf = nv30_screen_get_paramf;
   pscreen->get_shader_param = nv30_screen_get_shader_param;
   pscreen->context_create = nv30_context_create;
   pscreen->is_format_supported = nv30_screen_is_format_supported;
   nv30_resource_screen_init(pscreen);
   nouveau_screen_init_vdec(&screen->base);

   ret = nouveau_screen_init(&screen->base, dev);
   if (ret)
      FAIL_SCREEN_INIT("nv30_screen_init failed: %d\n", ret);

   screen->base.vidmem_bindings |= PIPE_BIND_VERTEX_BUFFER;
   screen->base.sysmem_bindings |= PIPE_BIND_VERTEX_BUFFER;
   if (oclass == NV40_3D_CLASS) {
      screen->base.vidmem_bindings |= PIPE_BIND_INDEX_BUFFER;
      screen->base.sysmem_bindings |= PIPE_BIND_INDEX_BUFFER;
   }

   return &screen->base;
}
