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

#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef CLOVER_ALLOW_SPIRV
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/linker.hpp>
#endif

#include "core/error.hpp"
#include "core/platform.hpp"
#include "invocation.hpp"
#include "llvm/util.hpp"
#include "pipe/p_state.h"
#include "util/algorithm.hpp"
#include "util/functional.hpp"
#include "util/u_math.h"

#include "spirv.hpp"

using namespace clover;

namespace {

   template<typename T>
   T get(const char *source, size_t index) {
      const uint32_t *word_ptr = reinterpret_cast<const uint32_t *>(source);
      return static_cast<T>(word_ptr[index]);
   }

   enum module::argument::type
   convertStorageClass(spv::StorageClass storage_class, std::string &err) {
      switch (storage_class) {
      case spv::StorageClass::Function:
         return module::argument::scalar;
      case spv::StorageClass::UniformConstant:
         return module::argument::constant;
      case spv::StorageClass::Workgroup:
         return module::argument::local;
      case spv::StorageClass::CrossWorkgroup:
         return module::argument::global;
      default:
         err += "Invalid storage type " +
                std::to_string(static_cast<int>(storage_class)) + "\n";
         throw build_error();
      }
   }

   enum module::argument::type
   convertImageType(spv::Id id, spv::Dim dim, spv::AccessQualifier access,
                    std::string &err) {
#define APPEND_DIM(d) \
      switch(access) { \
      case spv::AccessQualifier::ReadOnly: \
         return module::argument::image##d##_rd; \
      case spv::AccessQualifier::WriteOnly: \
         return module::argument::image##d##_wr; \
      default: \
         err += "Unsupported access qualifier " #d " for image " + \
                std::to_string(static_cast<int>(id)); \
         throw build_error(); \
      }

      switch (dim) {
      case spv::Dim::Dim2D:
         APPEND_DIM(2d)
      case spv::Dim::Dim3D:
         APPEND_DIM(3d)
      default:
         err += "Unsupported dimension " +
                std::to_string(static_cast<int>(dim)) + " for image " +
                std::to_string(static_cast<int>(id));
         throw build_error();
      }

#undef APPEND_DIM
   }

   void
   extract_kernels_arguments(module &m, std::string &err,
                             const std::vector<char> &source) {
      const size_t length = source.size() / sizeof(uint32_t);
      size_t i = 5u; // Skip header

      std::string kernel_name;
      uint32_t kernel_nb = 0u;
      std::vector<module::argument> args;
      uint32_t pointer_byte_size = 0u;

      std::unordered_map<spv::Id, std::string> kernels;
      std::unordered_map<spv::Id, module::argument> types;
      std::unordered_map<spv::Id, spv::Id> pointer_types;
      std::unordered_map<spv::Id, unsigned int> constants;
      std::unordered_set<spv::Id> packed_structures;
      std::unordered_map<spv::Id, std::vector<spv::FunctionParameterAttribute>>
         func_param_attr_map;

#define GET_OPERAND(type, operand_id) get<type>(source.data(), i + operand_id)

      while (i < length) {
         const auto desc_word = get<uint32_t>(source.data(), i);
         const auto opcode = static_cast<spv::Op>(desc_word & spv::OpCodeMask);
         const unsigned int num_operands = desc_word >> spv::WordCountShift;

         switch (opcode) {
         case spv::Op::OpEntryPoint:
            if (GET_OPERAND(spv::ExecutionModel, 1) != spv::ExecutionModel::Kernel)
               break;
            kernels.emplace(GET_OPERAND(spv::Id, 2), source.data() + (i + 3u) * sizeof(uint32_t));
            break;

         case spv::Op::OpMemoryModel:
            switch (GET_OPERAND(spv::AddressingModel, 1)) {
            case spv::AddressingModel::Physical32:
               pointer_byte_size = 4u;
               break;
            case spv::AddressingModel::Physical64:
               pointer_byte_size = 8u;
               break;
            }
            break;

         case spv::Op::OpDecorate: {
            const spv::Id id = GET_OPERAND(spv::Id, 1);
            const auto decoration = GET_OPERAND(spv::Decoration, 2);
            if (decoration == spv::Decoration::CPacked)
               packed_structures.emplace(id);
            else if (decoration == spv::Decoration::FuncParamAttr)
               func_param_attr_map[id].push_back(GET_OPERAND(spv::FunctionParameterAttribute, 3u));
            break;
         }

         case spv::Op::OpGroupDecorate: {
            const spv::Id group_id = GET_OPERAND(spv::Id, 1);
            if (packed_structures.count(group_id)) {
               for (unsigned int i = 2u; i < num_operands; ++i)
                  packed_structures.emplace(GET_OPERAND(spv::Id, i));
            }
            const auto func_param_attr_iter = func_param_attr_map.find(group_id);
            if (func_param_attr_iter != func_param_attr_map.end()) {
               for (unsigned int i = 2u; i < num_operands; ++i)
                  func_param_attr_map.emplace(GET_OPERAND(spv::Id, i),
                                              func_param_attr_iter->second);
            }
            break;
         }

         case spv::Op::OpConstant:
            // We only care about constants that represents the size of arrays.
            // If they are passed as argument, they will never be more than
            // 4GB-wide, and even if they did, a clover::module::argument size
            // is represented by an int.
            constants[GET_OPERAND(spv::Id, 2)] = GET_OPERAND(unsigned int, 3u);
            break;

         case spv::Op::OpTypeInt: // FALLTHROUGH
         case spv::Op::OpTypeFloat: {
            const auto size = GET_OPERAND(uint32_t, 2) / 8u;
            types[GET_OPERAND(spv::Id, 1)] = { module::argument::scalar,
                                               size, size, size,
                                               module::argument::zero_ext };
            break;
         }

         case spv::Op::OpTypeArray: {
            const auto id = GET_OPERAND(spv::Id, 1);
            const auto type_id = GET_OPERAND(spv::Id, 2);
            const auto types_iter = types.find(type_id);
            if (types_iter == types.end())
               break;

            const auto constant_id = GET_OPERAND(spv::Id, 3);
            const auto constants_iter = constants.find(constant_id);
            if (constants_iter == constants.end()) {
               err += "Constant " + std::to_string(constant_id) + " is missing\n";
               throw build_error();
            }
            const auto elem_size = types_iter->second.size;
            const auto elem_nbs = constants_iter->second;
            const auto size = elem_size * elem_nbs;
            types[id] = { module::argument::scalar, size, size,
                          types_iter->second.target_align,
                          module::argument::zero_ext };
            break;
         }

         case spv::Op::OpTypeStruct: {
            const auto id = GET_OPERAND(spv::Id, 1);
            const bool is_packed = packed_structures.count(id);

            unsigned struct_size = 0u;
            unsigned max_elem_size = 0u;
            for (unsigned j = 2u; j < num_operands; ++j) {
               const auto type_id = GET_OPERAND(spv::Id, j);
               const auto types_iter = types.find(type_id);
               if (types_iter == types.end())
                  break;

               const auto alignment = is_packed ? 1u
                                                : types_iter->second.target_align;
               const auto padding = (-struct_size) & (alignment - 1u);
               struct_size += padding + types_iter->second.target_size;
               max_elem_size = std::max(max_elem_size,
                                        types_iter->second.target_align);
            }
            if (!is_packed)
               struct_size += (-struct_size) & (max_elem_size - 1u);

            types[id] = { module::argument::scalar, struct_size, struct_size,
                          is_packed ? 1u
                                    : max_elem_size, module::argument::zero_ext
            };
            break;
         }

         case spv::Op::OpTypeVector: {
            const auto id = GET_OPERAND(spv::Id, 1);
            const auto type_id = GET_OPERAND(spv::Id, 2);
            const auto types_iter = types.find(type_id);
            if (types_iter == types.end())
               break;

            const auto elem_size = types_iter->second.size;
            const auto elem_nbs = GET_OPERAND(uint32_t, 3);
            const auto size = elem_size * elem_nbs;
            types[id] = { module::argument::scalar, size, size, size,
                          module::argument::zero_ext };
            break;
         }

         case spv::Op::OpTypeForwardPointer: // FALLTHROUGH
         case spv::Op::OpTypePointer: {
            const auto id = GET_OPERAND(spv::Id, 1);
            const auto storage_class = GET_OPERAND(spv::StorageClass, 2);
            // Input means this is for a builtin variable, which can not be
            // passed as an argument to a kernel.
            if (storage_class == spv::StorageClass::Input)
               break;
            types[id] = { convertStorageClass(storage_class, err),
                          sizeof(cl_mem), pointer_byte_size, pointer_byte_size,
                          module::argument::zero_ext
            };
            if (opcode == spv::Op::OpTypePointer)
               pointer_types[id] = GET_OPERAND(spv::Id, 3);
            break;
         }

         case spv::Op::OpTypeSampler:
            types[GET_OPERAND(spv::Id, 1)] = { module::argument::sampler,
                                               sizeof(cl_sampler)
            };
            break;

         case spv::Op::OpTypeImage: {
            const auto id = GET_OPERAND(spv::Id, 1);
            const auto dim = GET_OPERAND(spv::Dim, 3);
            const auto access = GET_OPERAND(spv::AccessQualifier, 9);
            types[id] = { convertImageType(id, dim, access, err),
                          sizeof(cl_mem), sizeof(cl_mem), sizeof(cl_mem),
                          module::argument::zero_ext
            };
            break;
         }

         case spv::Op::OpTypePipe: // FALLTHROUGH
         case spv::Op::OpTypeQueue: {
            err += "TypePipe and TypeQueue are valid SPIR-V 1.0 types, but are "
                   "not available in the currently supported OpenCL C version."
                   "\n";
            throw build_error();
         }

         case spv::Op::OpFunction: {
            const auto kernels_iter = kernels.find(GET_OPERAND(spv::Id, 2));
            if (kernels_iter != kernels.end())
               kernel_name = kernels_iter->second;
            break;
         }

         case spv::Op::OpFunctionParameter: {
            if (kernel_name.empty())
               break;

            const auto type_id = GET_OPERAND(spv::Id, 1);
            auto arg = types.find(type_id)->second;
            const auto &func_param_attr_iter =
               func_param_attr_map.find(GET_OPERAND(spv::Id, 2));
            if (func_param_attr_iter != func_param_attr_map.end()) {
               for (auto &i : func_param_attr_iter->second) {
                  switch (i) {
                  case spv::FunctionParameterAttribute::Sext:
                     arg.ext_type = module::argument::sign_ext;
                     break;
                  case spv::FunctionParameterAttribute::Zext:
                     arg.ext_type = module::argument::zero_ext;
                     break;
                  case spv::FunctionParameterAttribute::ByVal: {
                     const spv::Id ptr_type_id =
                        pointer_types.find(type_id)->second;
                     arg = types.find(ptr_type_id)->second;
                     break;
                  }
                  }
               }
            }
            args.emplace_back(arg);
            break;
         }

         case spv::Op::OpFunctionEnd:
            if (kernel_name.empty())
               break;
            m.syms.emplace_back(kernel_name, 0, kernel_nb, args);
            ++kernel_nb;
            kernel_name.clear();
            args.clear();
            break;

         default:
            break;
         }

         i += num_operands;
      }

#undef GET_OPERAND

   }

   std::string
   version_to_string(unsigned version) {
      return std::to_string((version >> 16u) & 0xff) + "." +
             std::to_string((version >>  8u) & 0xff);
   }

   void
   check_spirv_version(const char *binary, std::string &r_log) {
      const auto binary_version = get<uint32_t>(binary, 1u);
      if (binary_version <= spv::Version)
         return;

      r_log += "SPIR-V version " + version_to_string(binary_version) +
               " is not supported; supported versions <= " +
               version_to_string(spv::Version);
      throw build_error();
   }

   module::section
   make_text_section(const std::vector<char> &code,
                     enum module::section::type section_type,
                     module::section::flags_t section_flags) {
      const pipe_llvm_program_header header { uint32_t(code.size()) };
      module::section text { 0, section_type, section_flags, header.num_bytes,
                             {}
      };

      text.data.insert(text.data.end(), reinterpret_cast<const char *>(&header),
                       reinterpret_cast<const char *>(&header) + sizeof(header));
      text.data.insert(text.data.end(), code.begin(), code.end());

      return text;
   }

   bool
   check_capabilities(const device &dev, const std::vector<char> &source,
                      std::string &r_log) {
      const size_t length = source.size() / sizeof(uint32_t);
      size_t i = 5u; // Skip header

      while (i < length) {
         const auto desc_word = get<uint32_t>(source.data(), i);
         const auto opcode = static_cast<spv::Op>(desc_word & spv::OpCodeMask);
         const unsigned int num_operands = desc_word >> spv::WordCountShift;

         if (opcode != spv::Op::OpCapability)
            break;

         spv::Capability capability = get<spv::Capability>(source.data(), i + 1u);
         switch (capability) {
         // Mandatory capabilities
         case spv::Capability::Addresses:
         case spv::Capability::Float16Buffer:
         case spv::Capability::Groups:
         case spv::Capability::Int64:
         case spv::Capability::Int16:
         case spv::Capability::Int8:
         case spv::Capability::Kernel:
         case spv::Capability::Linkage:
         case spv::Capability::Vector16:
            break;
         // Optional capabilities
         case spv::Capability::ImageBasic:
         case spv::Capability::LiteralSampler:
         case spv::Capability::Sampled1D:
         case spv::Capability::Image1D:
         case spv::Capability::SampledBuffer:
         case spv::Capability::ImageBuffer:
            if (!dev.image_support()) {
               r_log += "Capability 'ImageBasic' is not supported.\n";
               return false;
            }
            break;
         case spv::Capability::Float64:
            if (!dev.has_doubles()) {
               r_log += "Capability 'Float64' is not supported.\n";
               return false;
            }
            break;
         // Enabled through extensions
         case spv::Capability::Float16:
            if (!dev.has_halves()) {
               r_log += "Capability 'Float16' is not supported.\n";
               return false;
            }
            break;
         default:
            r_log += "Capability '" +
                     std::to_string(static_cast<uint32_t>(capability)) +
                     "' is not supported.\n";
            return false;
         }

         i += num_operands;
      }

      return true;
   }

   bool
   check_extensions(const device &dev, const std::vector<char> &source,
                    std::string &r_log) {
      const size_t length = source.size() / sizeof(uint32_t);
      size_t i = 5u; // Skip header

      while (i < length) {
         const auto desc_word = get<uint32_t>(source.data(), i);
         const auto opcode = static_cast<spv::Op>(desc_word & spv::OpCodeMask);
         const unsigned int num_operands = desc_word >> spv::WordCountShift;

         if (opcode == spv::Op::OpCapability) {
            i += num_operands;
            continue;
         }
         if (opcode != spv::Op::OpExtension)
            break;

         const char *extension = source.data() + (i + 1u) * sizeof(uint32_t);
         const std::string device_extensions = dev.supported_extensions();
         const std::string platform_extensions =
            dev.platform.supported_extensions();
         if (device_extensions.find(extension) == std::string::npos &&
             platform_extensions.find(extension) == std::string::npos) {
            r_log += "Extension '" + std::string(extension) +
                     "' is not supported.\n";
            return false;
         }

         i += num_operands;
      }

      return true;
   }

   // Copies the input binary and convert it to the endianness of the host CPU.
   std::vector<char>
   spirv_to_cpu(const std::vector<char> &binary)
   {
      const uint32_t first_word = get<uint32_t>(binary.data(), 0u);
      if (first_word == spv::MagicNumber)
         return binary;

      std::vector<char> cpu_endianness_binary(binary.size());
      for (size_t i = 0; i < (binary.size() / 4u); ++i) {
         const uint32_t word = get<uint32_t>(binary.data(), i);
         reinterpret_cast<uint32_t *>(cpu_endianness_binary.data())[i] =
            util_bswap32(word);
      }

      return cpu_endianness_binary;
   }

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
   return (first_word == spv::MagicNumber) ||
          (util_bswap32(first_word) == spv::MagicNumber);
}

module
clover::spirv::process_program(const std::vector<char> &binary,
                               const device &dev, bool validate,
                               std::string &r_log) {
   std::vector<char> source = spirv_to_cpu(binary);

   if (validate && !is_valid_spirv(
         reinterpret_cast<const uint32_t *>(source.data()),
         source.size() / 4u, dev.device_version(),
         [&r_log](const char *log){ r_log += std::string(log); }))
      throw build_error();

   check_spirv_version(source.data(), r_log);

   if (!check_capabilities(dev, source, r_log))
      throw build_error();
   if (!check_extensions(dev, source, r_log))
      throw build_error();

   module m;
   extract_kernels_arguments(m, r_log, source);
   m.secs.push_back(make_text_section(source,
                                      module::section::text_intermediate,
                                      module::section::flags_t::none));

   return m;
}

#ifdef CLOVER_ALLOW_SPIRV
module
clover::spirv::link_program(const std::vector<module> &modules,
                            const device &dev, const std::string &opts,
                            std::string &r_log) {
   std::vector<std::string> options = clover::llvm::tokenize(opts);

   spvtools::LinkerOptions linker_options;
   const bool create_library = count("-create-library", options);
   erase_if(equals("-create-library"), options);

   const bool enable_link = count("-enable-link-options", options);
   erase_if(equals("-enable-link-options"), options);
   if (enable_link && !linker_options.GetCreateLibrary()) {
      r_log += "SPIR-V linker: '-enable-link-options' cannot be used without '-create-library'\n";
      throw invalid_build_options_error();
   }

   if (!options.empty()) {
      r_log += "SPIR-V linker: Ignoring the following link options: ";
      for (const auto &opt : options)
         r_log += "'" + opt + "' ";
   }

   linker_options.SetCreateLibrary(create_library);

   module m;

   const auto section_type = create_library ? module::section::text_library :
                                              module::section::text_executable;

   std::vector<const uint32_t *> sections;
   sections.reserve(modules.size());
   std::vector<size_t> lengths;
   lengths.reserve(modules.size());

   auto const validator_consumer = [&r_log](spv_message_level_t level,
                                            const char * /* source */,
                                            const spv_position_t &position,
                                            const char *message) {
      r_log += format_validator_msg(level, position, message);
   };

   for (const auto &mod : modules) {
      const module::section *msec = nullptr;
      try {
         msec = &find([&](const module::section &sec) {
                  return sec.type == module::section::text_intermediate ||
                         sec.type == module::section::text_library;
               }, mod.secs);
      } catch (const std::out_of_range &e) {
         // We should never reach this as validate_link_devices already checked
         // that a binary was present.
         assert(false);
      }

      const auto c_il = msec->data.data() +
                        sizeof(struct pipe_llvm_program_header);
      const auto length = msec->size;

      check_spirv_version(c_il, r_log);

      sections.push_back(reinterpret_cast<const uint32_t *>(c_il));
      lengths.push_back(length / sizeof(uint32_t));
   }

   std::vector<uint32_t> linked_binary;

   const std::string opencl_version = dev.device_version();
   const spv_target_env target_env =
      convert_opencl_str_to_target_env(opencl_version);

   const spvtools::MessageConsumer consumer = validator_consumer;
   spvtools::Context context(target_env);
   context.SetMessageConsumer(std::move(consumer));

   if (Link(context, sections.data(), lengths.data(), sections.size(),
            &linked_binary, linker_options) != SPV_SUCCESS)
      throw build_error();

   if (!is_valid_spirv(linked_binary.data(), linked_binary.size(),
                       opencl_version,
                       [&r_log](const char *log){ r_log += std::string(log); }))
      throw build_error();

   for (const auto &mod : modules)
      m.syms.insert(m.syms.end(), mod.syms.begin(), mod.syms.end());

   module::section::flags_t flags = module::section::flags_t::none;
   if (enable_link)
      flags = module::section::flags_t::allow_link_options;

   m.secs.emplace_back(make_text_section({
            reinterpret_cast<char *>(linked_binary.data()),
            reinterpret_cast<char *>(linked_binary.data()) +
               linked_binary.size() * sizeof(uint32_t) }, section_type, flags));

   return m;
}

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
module
clover::spirv::link_program(const std::vector<module> &/*modules*/,
                            const device &/*dev*/, const std::string &/*opts*/,
                            std::string &r_log) {
   r_log += "SPIRV-Tools and llvm-spirv are required for linking SPIR-V binaries.\n";
   throw error(CL_LINKER_NOT_AVAILABLE);
}

bool
clover::spirv::is_valid_spirv(const uint32_t * /*binary*/, size_t /*length*/,
                              const std::string &/*opencl_version*/,
                              const context::notify_action &/*notify*/) {
   return false;
}
#endif
