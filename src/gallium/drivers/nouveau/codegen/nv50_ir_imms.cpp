#include "codegen/nv50_ir.h"

namespace nv50_ir {

/*
 * This is a list of most common constants in shaders we can't fold into
 * the instruction as long or short immediates, but where we can load them
 * through a const buffer.
 *
 * This list is biased towards our shader-db but generally only contains
 * "commonly" used numbers
 */
static const uint32_t imms[] = {
   // "common" numbers
   0x00000001, // 1
   0x00000002, // 2
   0x2edbe6ff, // 1e-10
   0x322bcc77, // 1e-8
   0x33d6bf95, // 1e-7
   0x358637bd, // 1e-6
   0x3727c5ac, // 1e-5
   0x38d1b717, // 1e-4
   0x3a83126f, // 1e-3
   0x3b03126f, // 0.002
   0x3b23d70a, // 0.0025
   0x3b808081, // 1/255
   0x3c007fed, // 1/127.5
   0x3c008081, // 1/127.5
   0x3c008083, // 1/127.5
   0x3c088865, // 1/120
   0x3c23d70a, // 0.01
   0x3ca3d70a, // 0.02
   0x3d23d70a, // 0.04
   0x3d4ccccd, // 0.05
   0x3d75c28f, // 0.06
   0x3dcccccd, // 0.1
   0x3de147ae, // 0.11
   0x3de38e39, // 1/9
   0x3e19999a, // 0.15
   0x3e4ccccd, // 0.2
   0x3e638e39, // 2/9
   0x3e99999a, // 0.3
   0x3ea8f5c3, // 0.33
   0x3eaa7efa, // 0.333
   0x3eaaa64c, // 0.3333
   0x3eaaaa9f, // 1/3
   0x3eaaaaab, // 1/3
   0x3ecccccd, // 0.4
   0x3eff7cee, // 0.499
   0x3f000000, // 0.5
   0x3f19999a, // 0.6
   0x3f333333, // 0.7
   0x3f4ccccd, // 0.8
   0x3f555555, // 5/6
   0x3f666666, // 0.9
   0x3f7fbe77, // 0.999
   0x3f800000, // 1.0
   0x3f8ccccd, // 1.1
   0x3f99999a, // 1.2
   0x40000000, // 2.0
   0x40400000, // 3.0
   0x40800000, // 4.0
   0x41200000, // 10.0
   0x477fff00, // 65535.0
   0x7f800000, // inf
   0x80000001, // INT_MIN + 1
   0xbf800000, // -1.0

   // special numbers
   0x3e22f983, // 0.5/pi
   0x3e22f987, // 0.5/pi
   0x3fc90da4, // 3.141456/2
   0x3fc90fdb, // pi/2
   0x40490fd0, // 3.141590
   0x40490fdb, // pi
   0x40490fdc, // pi
   0x40c90fda, // 2pi
   0x40c90fdb, // 2pi
   0x40c90fe4, // 6.283190
   0x41490fdb, // 4pi

   // glsl/nir lowering
   0x3d5be101, //  0.0536813784310406
   0x3d981627, //  0.074261 (optimized asin)
   0x3df0555d, //  0.1173503194786851
   0x3e468bc1, //  0.1938924977115610
   0x3eaa5476, //  0.3326756418091246
   0x3f7ffea5, //  0.9999793128310355
   0xbc996e30, // -0.018729 (optimized asin)
   0xbe5bc094, //  M_PI_4f - 1.0f
   0xbe593484, // -0.212114 (optimized asin)

   // sRGB -> linear
   0x3d25aee6, //  0.04045

   // linear -> sRGB
   0x3b4d2e1c, //  0.0031308
   0x3f870a3d, //  1.055
   0xbd6147ae, // -0.055

   // BT.601 luma: 0.299R 0.587G 0.114B
   0x3e991687, // 0.299
   0x3f1645a2, // 0.587
   0x3f170a3d, // 0.59
   0x3de978d5, // 0.114

   // relative luminance: 0.2126R + 0.7152G + 0.0722B
   0x3d93dd98, // 0.0722
   0x3f371759, // 0.7152

   // some lightmap stuff y = dot(0.816496,0.0,0.577350, x) zw = dot(-0.408248, 0.707106,0.577350, x)
   0x3f13cd36, //  0.577350
   0x3f13cd3a, //  0.577350
   0x3f3504f3, //  0.707106
   0x3f3504f7, //  0.707106
   0x3f5105ec, //  0.816496
   0xbed105ec, // -0.408248
   0xbed105e2, // -0.408248

   // game engine constants
   0x3c4d2cc2, //  0.012523 unity
   0x3e9c5112, //  0.305306 unity
   0x3f2ea2c4, //  0.682171 unity
   0x3f4c422a, //  0.797885 unity
   0x40cd154a, //  6.408849 unity
   0xb9500c47, // -0.000198 unity
};

static std::unordered_map<uint32_t, uint16_t> generate_imms() {

   STATIC_ASSERT(sizeof(imms) < 0x1000);
   std::unordered_map<uint32_t, uint16_t> res;

   for (uint16_t i = 0; i < ARRAY_SIZE(imms); ++i)
      res.emplace(imms[i], i * 4);

   return res;
}

const std::unordered_map<uint32_t, uint16_t> Program::imms = generate_imms();

}

extern "C" {

void nv50_ir_get_imms_buffer(const uint8_t **imms, uint16_t *size)
{
   *imms = (const uint8_t*)nv50_ir::imms;
   *size = sizeof(nv50_ir::imms);
}

}
