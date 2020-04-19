#include "codegen/nv50_ir.h"

namespace nv50_ir {

/*
 * This is a list of most common constants in shaders we can't fold into
 * the instruction as long or short immediates, but where we can load them
 * through a const buffer.
 *
 * This list is biased towards our shader-db and contains all constants where
 * the above is true for more then 500 shaders.
 */
static const uint32_t imms[] = {
   0x2edbe6ff,
   0x33d6bf95,
   0x358637bd,
   0x3727c5ac,
   0x38d1b717,
   0x3a83126f,
   0x3b808081,
   0x3c007fed,
   0x3c23d70a,
   0x3d4ccccd,
   0x3d93dd98,
   0x3dcccccd,
   0x3de147ae,
   0x3de978d5,
   0x3e4ccccd,
   0x3e99999a,
   0x3eaaaaab,
   0x3ecccccd,
   0x3f000000,
   0x3f13cd3a,
   0x3f1645a2,
   0x3f170a3d,
   0x3f333333,
   0x3f371759,
   0x3f7fbe77,
   0x3f800000,
   0x3f870a3d,
   0x477fff00,
   0xb8d1b717,
   0xbed105ec,
   0xbeff7cee,
   0xbf000000,
   0xbf800000,
};

static std::unordered_map<uint32_t, uint16_t> generate_imms() {
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
