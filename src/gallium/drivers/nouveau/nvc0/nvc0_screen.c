/*
 * Copyright 2010 Christoph Bumiller
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
 */

#include <xf86drm.h>
#include <nouveau_drm.h>
#include <nvif/class.h>
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_screen.h"
#include "pipe/p_screen.h"

#include "nouveau_vp3_video.h"

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_screen.h"

#include "nv50/g80_texture.xml.h"

static boolean
nvc0_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned bindings)
{
   const struct util_format_description *desc = util_format_description(format);

   if (sample_count > 8)
      return false;
   if (!(0x117 & (1 << sample_count))) /* 0, 1, 2, 4 or 8 */
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* Short-circuit the rest of the logic -- this is used by the state tracker
    * to determine valid MS levels in a no-attachments scenario.
    */
   if (format == PIPE_FORMAT_NONE && bindings & PIPE_BIND_RENDER_TARGET)
      return true;

   if ((bindings & PIPE_BIND_SAMPLER_VIEW) && (target != PIPE_BUFFER))
      if (util_format_get_blocksizebits(format) == 3 * 32)
         return false;

   if (bindings & PIPE_BIND_LINEAR)
      if (util_format_is_depth_or_stencil(format) ||
          (target != PIPE_TEXTURE_1D &&
           target != PIPE_TEXTURE_2D &&
           target != PIPE_TEXTURE_RECT) ||
          sample_count > 1)
         return false;

   /* Restrict ETC2 and ASTC formats here. These are only supported on GK20A.
    */
   if ((desc->layout == UTIL_FORMAT_LAYOUT_ETC ||
        desc->layout == UTIL_FORMAT_LAYOUT_ASTC) &&
       /* The claim is that this should work on GM107 but it doesn't. Need to
        * test further and figure out if it's a nouveau issue or a HW one.
       nouveau_screen(pscreen)->class_3d < GM107_3D_CLASS &&
        */
       nouveau_screen(pscreen)->class_3d != NVEA_3D_CLASS)
      return false;

   /* shared is always supported */
   bindings &= ~(PIPE_BIND_LINEAR |
                 PIPE_BIND_SHARED);

   if (bindings & PIPE_BIND_SHADER_IMAGE) {
      if (format == PIPE_FORMAT_B8G8R8A8_UNORM &&
          nouveau_screen(pscreen)->class_3d < NVE4_3D_CLASS) {
         /* This should work on Fermi, but for currently unknown reasons it
          * does not and results in breaking reads from pbos. */
         return false;
      }
   }

   return (( nvc0_format_table[format].usage |
            nvc0_vertex_format[format].usage) & bindings) == bindings;
}

static int
nvc0_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   const uint16_t class_3d = nouveau_screen(pscreen)->class_3d;
   struct nouveau_device *dev = nouveau_screen(pscreen)->device;

   switch (param) {
   /* non-boolean caps */
   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 15;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 12;
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return 2048;
   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;
   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return -32;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return 31;
   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return 128 * 1024 * 1024;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 430;
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 430;
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return 8;
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      return 1;
   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
   case PIPE_CAP_RASTERIZER_SUBPIXEL_BITS:
      return 8;
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return 4;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return 128;
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return 1024;
   case PIPE_CAP_MAX_VERTEX_STREAMS:
      return 4;
   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 32;
   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 1 << 27;
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 256;
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      if (class_3d < GM107_3D_CLASS)
         return 256; /* IMAGE bindings require alignment to 256 */
      return 16;
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      return 16;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return NOUVEAU_MIN_BUFFER_MAP_ALIGN;
   case PIPE_CAP_MAX_VIEWPORTS:
      return NVC0_MAX_VIEWPORTS;
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
      return PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;
   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
      return 30;
   case PIPE_CAP_MAX_WINDOW_RECTANGLES:
      return NVC0_MAX_WINDOW_RECTANGLES;
   case PIPE_CAP_MAX_CONSERVATIVE_RASTER_SUBPIXEL_PRECISION_BIAS:
      return class_3d >= GM200_3D_CLASS ? 8 : 0;

   /* supported caps */
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_SM3:
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_PRIMITIVE_RESTART:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_START_INSTANCE:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
   case PIPE_CAP_TEXTURE_GATHER_SM5:
   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_SHAREABLE_SHADERS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_DRAW_PARAMETERS:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
   case PIPE_CAP_CULL_DISTANCE:
   case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
   case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
   case PIPE_CAP_TGSI_VOTE:
   case PIPE_CAP_POLYGON_OFFSET_UNITS_UNSCALED:
   case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
   case PIPE_CAP_TGSI_MUL_ZERO_WINS:
   case PIPE_CAP_DOUBLES:
   case PIPE_CAP_INT64:
   case PIPE_CAP_TGSI_TEX_TXF_LZ:
   case PIPE_CAP_TGSI_CLOCK:
   case PIPE_CAP_COMPUTE:
   case PIPE_CAP_CAN_BIND_CONST_BUFFER_AS_VERTEX:
   case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
   case PIPE_CAP_QUERY_SO_OVERFLOW:
      return 1;
   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return nouveau_screen(pscreen)->vram_domain & NOUVEAU_BO_VRAM ? 1 : 0;
   case PIPE_CAP_TGSI_FS_FBFETCH:
      return class_3d >= NVE4_3D_CLASS; /* needs testing on fermi */
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_TGSI_BALLOT:
   case PIPE_CAP_BINDLESS_TEXTURE:
      return class_3d >= NVE4_3D_CLASS;
   case PIPE_CAP_POLYGON_MODE_FILL_RECTANGLE:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
   case PIPE_CAP_POST_DEPTH_COVERAGE:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_TRIANGLES:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_POINTS_LINES:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_DEPTH_COVERAGE:
   case PIPE_CAP_PROGRAMMABLE_SAMPLE_LOCATIONS:
      return class_3d >= GM200_3D_CLASS;
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_TRIANGLES:
      return class_3d >= GP100_3D_CLASS;

   /* unsupported caps */
   case PIPE_CAP_DEPTH_CLIP_DISABLE_SEPARATE:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_FAKE_SW_MSAA:
   case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
   case PIPE_CAP_VERTEXID_NOBASE:
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
   case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_GENERATE_MIPMAP:
   case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
   case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
   case PIPE_CAP_QUERY_MEMORY_INFO:
   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
   case PIPE_CAP_TGSI_CAN_READ_OUTPUTS:
   case PIPE_CAP_NATIVE_FENCE_FD:
   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
   case PIPE_CAP_INT64_DIVMOD:
   case PIPE_CAP_SPARSE_BUFFER_PAGE_SIZE:
   case PIPE_CAP_NIR_SAMPLERS_AS_DEREF:
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
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_POINTS_LINES:
   case PIPE_CAP_MAX_TEXTURE_UPLOAD_MEMORY_BUDGET:
   case PIPE_CAP_MAX_COMBINED_SHADER_BUFFERS:
   case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTERS:
   case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTER_BUFFERS:
      return 0;

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
      debug_printf("%s: unhandled cap %d\n", __func__, param);
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static int
nvc0_screen_get_shader_param(struct pipe_screen *pscreen,
                             enum pipe_shader_type shader,
                             enum pipe_shader_cap param)
{
   const uint16_t class_3d = nouveau_screen(pscreen)->class_3d;

   switch (shader) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_COMPUTE:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
      break;
   default:
      return 0;
   }

   switch (param) {
   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_TGSI;
   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return 1 << PIPE_SHADER_IR_TGSI;
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 16384;
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return 16;
   case PIPE_SHADER_CAP_MAX_INPUTS:
      if (shader == PIPE_SHADER_VERTEX)
         return 32;
      /* NOTE: These only count our slots for GENERIC varyings.
       * The address space may be larger, but the actual hard limit seems to be
       * less than what the address space layout permits, so don't add TEXCOORD,
       * COLOR, etc. here.
       */
      if (shader == PIPE_SHADER_FRAGMENT)
         return 0x1f0 / 16;
      /* Actually this counts CLIPVERTEX, which occupies the last generic slot,
       * and excludes 0x60 per-patch inputs.
       */
      return 0x200 / 16;
   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      return 32;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return NVC0_MAX_CONSTBUF_SIZE;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return NVC0_MAX_PIPE_CONSTBUFS;
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      return shader != PIPE_SHADER_FRAGMENT;
   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return 1;
   case PIPE_SHADER_CAP_MAX_TEMPS:
      return NVC0_CAP_MAX_PROGRAM_TEMPS;
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_SUBROUTINES:
      return 1;
   case PIPE_SHADER_CAP_INTEGERS:
      return 1;
   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      return 1;
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
   case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_FP16:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
      return 0;
   case PIPE_SHADER_CAP_SCALAR_ISA:
      return 1;
   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      return NVC0_MAX_BUFFERS;
   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      return (class_3d >= NVE4_3D_CLASS) ? 32 : 16;
   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return (class_3d >= NVE4_3D_CLASS) ? 32 : 16;
   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32;
   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      if (class_3d >= NVE4_3D_CLASS)
         return NVC0_MAX_IMAGES;
      if (shader == PIPE_SHADER_FRAGMENT || shader == PIPE_SHADER_COMPUTE)
         return NVC0_MAX_IMAGES;
      return 0;
   default:
      NOUVEAU_ERR("unknown PIPE_SHADER_CAP %d\n", param);
      return 0;
   }
}

static float
nvc0_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   const uint16_t class_3d = nouveau_screen(pscreen)->class_3d;

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 10.0f;
   case PIPE_CAPF_MAX_POINT_WIDTH:
      return 63.0f;
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 63.375f;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 16.0f;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 15.0f;
   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
      return 0.0f;
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
      return class_3d >= GM200_3D_CLASS ? 0.75f : 0.0f;
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return class_3d >= GM200_3D_CLASS ? 0.25f : 0.0f;
   }

   NOUVEAU_ERR("unknown PIPE_CAPF %d\n", param);
   return 0.0f;
}

static int
nvc0_screen_get_compute_class(struct nvc0_screen *screen)
{
   switch (screen->base.device->chipset & ~0xf) {
   case 0xc0:
   case 0xd0:
      return nvc0_get_compute_class(screen);
   case 0xe0:
   case 0xf0:
   case 0x100:
   case 0x110:
   case 0x120:
   case 0x130:
      return nve4_get_compute_class(screen);
   default:
      return -1;
   }
}

static int
nvc0_screen_get_compute_param(struct pipe_screen *pscreen,
                              enum pipe_shader_ir ir_type,
                              enum pipe_compute_cap param, void *data)
{
   struct nvc0_screen *screen = nvc0_screen(pscreen);
   const uint16_t obj_class = nvc0_screen_get_compute_class(screen);

#define RET(x) do {                  \
   if (data)                         \
      memcpy(data, x, sizeof(x));    \
   return sizeof(x);                 \
} while (0)

   switch (param) {
   case PIPE_COMPUTE_CAP_GRID_DIMENSION:
      RET((uint64_t []) { 3 });
   case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
      if (obj_class >= NVE4_COMPUTE_CLASS) {
         RET(((uint64_t []) { 0x7fffffff, 65535, 65535 }));
      } else {
         RET(((uint64_t []) { 65535, 65535, 65535 }));
      }
   case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
      RET(((uint64_t []) { 1024, 1024, 64 }));
   case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
      RET((uint64_t []) { 1024 });
   case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
      if (obj_class >= NVE4_COMPUTE_CLASS) {
         RET((uint64_t []) { 1024 });
      } else {
         RET((uint64_t []) { 512 });
      }
   case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE: /* g[] */
      RET((uint64_t []) { 1ULL << 40 });
   case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE: /* s[] */
      switch (obj_class) {
      case GM200_COMPUTE_CLASS:
         RET((uint64_t []) { 96 << 10 });
         break;
      case GM107_COMPUTE_CLASS:
         RET((uint64_t []) { 64 << 10 });
         break;
      default:
         RET((uint64_t []) { 48 << 10 });
         break;
      }
   case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE: /* l[] */
      RET((uint64_t []) { 512 << 10 });
   case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE: /* c[], arbitrary limit */
      RET((uint64_t []) { 4096 });
   case PIPE_COMPUTE_CAP_SUBGROUP_SIZE:
      RET((uint32_t []) { 32 });
   case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
      RET((uint64_t []) { 1ULL << 40 });
   case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
      RET((uint32_t []) { 0 });
   case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
      RET((uint32_t []) { screen->mp_count_compute });
   case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
      RET((uint32_t []) { 512 }); /* FIXME: arbitrary limit */
   case PIPE_COMPUTE_CAP_ADDRESS_BITS:
      RET((uint32_t []) { 64 });
   default:
      return 0;
   }

#undef RET
}

static void
nvc0_screen_get_sample_pixel_grid(struct pipe_screen *pscreen,
                                  unsigned sample_count,
                                  unsigned *width, unsigned *height)
{
   switch (sample_count) {
   case 0:
   case 1:
      /* this could be 4x4, but the GL state tracker makes it difficult to
       * create a 1x MSAA texture and smaller grids save CB space */
      *width = 2;
      *height = 4;
      break;
   case 2:
      *width = 2;
      *height = 4;
      break;
   case 4:
      *width = 2;
      *height = 2;
      break;
   case 8:
      *width = 1;
      *height = 2;
      break;
   default:
      assert(0);
   }
}

static void
nvc0_screen_destroy(struct pipe_screen *pscreen)
{
   struct nvc0_screen *screen = nvc0_screen(pscreen);

   if (!nouveau_drm_screen_unref(&screen->base))
      return;

   if (screen->pm.prog) {
      screen->pm.prog->code = NULL; /* hardcoded, don't FREE */
      nvc0_program_destroy(NULL, screen->pm.prog);
      FREE(screen->pm.prog);
   }

   nouveau_screen_fini(&screen->base);

   FREE(screen);
}

#define FAIL_SCREEN_INIT(str, err)                    \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      goto fail;                                      \
   } while(0)

struct nouveau_screen *
nvc0_screen_create(struct nouveau_device *dev)
{
   struct nvc0_screen *screen;
   struct pipe_screen *pscreen;
   uint64_t value;
   uint32_t obj_class;
   int ret;

   switch (dev->chipset & ~0xf) {
   case 0xc0:
   case 0xd0:
   case 0xe0:
   case 0xf0:
   case 0x100:
   case 0x110:
   case 0x120:
   case 0x130:
      break;
   default:
      return NULL;
   }

   screen = CALLOC_STRUCT(nvc0_screen);
   if (!screen)
      return NULL;
   pscreen = &screen->base.base;
   pscreen->destroy = nvc0_screen_destroy;

   ret = nouveau_screen_init(&screen->base, dev);
   if (ret)
      FAIL_SCREEN_INIT("Base screen init failed: %d\n", ret);

   screen->base.vidmem_bindings |= PIPE_BIND_CONSTANT_BUFFER |
      PIPE_BIND_SHADER_BUFFER |
      PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER |
      PIPE_BIND_COMMAND_ARGS_BUFFER | PIPE_BIND_QUERY_BUFFER;
   screen->base.sysmem_bindings |=
      PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;

   if (screen->base.vram_domain & NOUVEAU_BO_GART) {
      screen->base.sysmem_bindings |= screen->base.vidmem_bindings;
      screen->base.vidmem_bindings = 0;
   }

   pscreen->context_create = nvc0_create;
   pscreen->is_format_supported = nvc0_screen_is_format_supported;
   pscreen->get_param = nvc0_screen_get_param;
   pscreen->get_shader_param = nvc0_screen_get_shader_param;
   pscreen->get_paramf = nvc0_screen_get_paramf;
   pscreen->get_sample_pixel_grid = nvc0_screen_get_sample_pixel_grid;
   pscreen->get_driver_query_info = nvc0_screen_get_driver_query_info;
   pscreen->get_driver_query_group_info = nvc0_screen_get_driver_query_group_info;

   nvc0_screen_init_resource_functions(pscreen);

   screen->base.base.get_video_param = nouveau_vp3_screen_get_video_param;
   screen->base.base.is_video_format_supported = nouveau_vp3_screen_video_supported;

   switch (dev->chipset & ~0xf) {
   case 0x130:
      switch (dev->chipset) {
      case 0x130:
      case 0x13b:
         obj_class = GP100_3D_CLASS;
         break;
      default:
         obj_class = GP102_3D_CLASS;
         break;
      }
      break;
   case 0x120:
      obj_class = GM200_3D_CLASS;
      break;
   case 0x110:
      obj_class = GM107_3D_CLASS;
      break;
   case 0x100:
   case 0xf0:
      obj_class = NVF0_3D_CLASS;
      break;
   case 0xe0:
      switch (dev->chipset) {
      case 0xea:
         obj_class = NVEA_3D_CLASS;
         break;
      default:
         obj_class = NVE4_3D_CLASS;
         break;
      }
      break;
   case 0xd0:
      obj_class = NVC8_3D_CLASS;
      break;
   case 0xc0:
   default:
      switch (dev->chipset) {
      case 0xc8:
         obj_class = NVC8_3D_CLASS;
         break;
      case 0xc1:
         obj_class = NVC1_3D_CLASS;
         break;
      default:
         obj_class = NVC0_3D_CLASS;
         break;
      }
      break;
   }
   screen->base.class_3d = obj_class;

   if (screen->base.drm->version >= 0x01000101) {
      ret = nouveau_getparam(dev, NOUVEAU_GETPARAM_GRAPH_UNITS, &value);
      if (ret)
         FAIL_SCREEN_INIT("NOUVEAU_GETPARAM_GRAPH_UNITS failed: %d\n", ret);
   } else {
      if (dev->chipset >= 0xe0 && dev->chipset < 0xf0)
         value = (8 << 8) | 4;
      else
         value = (16 << 8) | 4;
   }
   screen->gpc_count = value & 0x000000ff;
   screen->mp_count = value >> 8;
   screen->mp_count_compute = screen->mp_count;

   screen->base.base.get_compute_param = nvc0_screen_get_compute_param;

   return &screen->base;

fail:
   screen->base.base.context_create = NULL;
   return &screen->base;
}
