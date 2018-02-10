//
// Copyright 2018 Pierre Moreau
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include "invocation.hpp"

#ifdef CLOVER_ALLOW_SPIRV
#include <spirv-tools/libspirv.hpp>
#endif

#include "util/u_math.h"

#include "compiler/spirv/spirv.h"

using namespace clover;

namespace {

#ifdef CLOVER_ALLOW_SPIRV
   std::string
   format_validator_msg(spv_message_level_t level,
                        const spv_position_t &position, const char *message) {
      auto const level_to_string = [](spv_message_level_t level){
#define LVL2STR(lvl) case SPV_MSG_##lvl: return std::string(#lvl)
         switch (level) {
            LVL2STR(FATAL);
            LVL2STR(INTERNAL_ERROR);
            LVL2STR(ERROR);
            LVL2STR(WARNING);
            LVL2STR(INFO);
            LVL2STR(DEBUG);
         }
#undef LVL2STR
         return std::string();
      };
      return "[" + level_to_string(level) + "] At word No." +
             std::to_string(position.index) + ": \"" + message + "\"\n";
   }

   spv_target_env
   convert_opencl_str_to_target_env(const std::string &opencl_version) {
      if (opencl_version == "2.2") {
         return SPV_ENV_OPENCL_2_2;
      } else if (opencl_version == "2.1") {
         return SPV_ENV_OPENCL_2_1;
      } else if (opencl_version == "2.0") {
         return SPV_ENV_OPENCL_2_0;
      } else if (opencl_version == "1.2" ||
                 opencl_version == "1.1" ||
                 opencl_version == "1.0") {
         // SPIR-V is only defined for OpenCL >= 1.2, however some drivers
         // might use it with OpenCL 1.0 and 1.1.
         return SPV_ENV_OPENCL_1_2;
      } else {
         throw build_error("Invalid OpenCL version");
      }
   }
#endif

}

bool
clover::spirv::is_binary_spirv(const char *il, size_t length)
{
   const uint32_t *binary = reinterpret_cast<const uint32_t*>(il);

   // A SPIR-V binary is at the very least 5 32-bit words, which represent the
   // SPIR-V header.
   if (length < 20u)
      return false;

   const uint32_t first_word = binary[0u];
   return (first_word == SpvMagicNumber) ||
          (util_bswap32(first_word) == SpvMagicNumber);
}

#ifdef CLOVER_ALLOW_SPIRV
bool
clover::spirv::is_valid_spirv(const uint32_t *binary, size_t length,
                              const std::string &opencl_version,
                              const context::notify_action &notify) {
   auto const validator_consumer = [&notify](spv_message_level_t level,
                                             const char * /* source */,
                                             const spv_position_t &position,
                                             const char *message) {
      if (!notify)
         return;

      std::string str_level;
      switch (level) {
#define LVL2STR(lvl) case SPV_MSG_##lvl: str_level = std::string(#lvl)
         LVL2STR(FATAL);
         LVL2STR(INTERNAL_ERROR);
         LVL2STR(ERROR);
         LVL2STR(WARNING);
         LVL2STR(INFO);
         LVL2STR(DEBUG);
#undef LVL2STR
      }
      const std::string log = "[" + str_level + "] At word No." +
                              std::to_string(position.index) + ": \"" +
                              message + "\"";
      notify(log.c_str());
   };

   const spv_target_env target_env =
      convert_opencl_str_to_target_env(opencl_version);
   spvtools::SpirvTools spvTool(target_env);
   spvTool.SetMessageConsumer(validator_consumer);

   return spvTool.Validate(binary, length);
}
#else
bool
clover::spirv::is_valid_spirv(const uint32_t * /*binary*/, size_t /*length*/,
                              const std::string &/*opencl_version*/,
                              const context::notify_action &/*notify*/) {
   return false;
}
#endif
