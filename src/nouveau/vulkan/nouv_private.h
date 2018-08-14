#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#ifndef NDEBUG
#define __gen_validate_value(x) VALGRIND_CHECK_MEM_IS_DEFINED(&(x), sizeof(x))
#endif
#else
#define VG(x)
#endif

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_intel.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vk_android_native_buffer.h>

#include "util/macros.h"
#include "vk_debug_report.h"
#include "wsi_common.h"

#include "nouv_entrypoints.h"
#include "nouv_extensions.h"

#define NOUV_MAX_DRM_DEVICES 8

enum nouv_mem_heap {
   NOUV_MEM_HEAP_VRAM,
   NOUV_MEM_HEAP_GART,
   NOUV_MEM_HEAP_COUNT
};

enum nouv_mem_type {
   NOUV_MEM_TYPE_VRAM,
   NOUV_MEM_TYPE_GART,
   NOUV_MEM_TYPE_COUNT
};

struct nouv_device;
struct nouv_physical_device;

struct nouv_cmd_buffer {
   VK_LOADER_DATA _loader_data;
   struct nouv_device *device;
};

struct nouv_device {
   VK_LOADER_DATA _loader_data;
   VkAllocationCallbacks alloc;

   struct nouv_device_extension_table enabled_extensions;
   struct nouv_dispatch_table dispatch;
   struct nouv_instance *instance;
};

struct nouv_physical_device {
   VK_LOADER_DATA _loader_data;
   struct nouv_device_extension_table supported_extensions;
   struct nouv_instance *instance;
   int local_fd;
   int master_fd;
   VkPhysicalDeviceMemoryProperties memory_properties;
   enum nouv_mem_type mem_type_indices[NOUV_MEM_TYPE_COUNT];
   char path[20];
   struct nouv_winsys *ws;
   struct wsi_device wsi_device;

   struct {
      char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
      uint16_t chipset;
      uint16_t device_id;
      uint64_t gart_size;
      uint64_t vram_size;
   } info;
};

struct nouv_instance {
    VK_LOADER_DATA _loader_data;
    VkAllocationCallbacks alloc;

    uint32_t apiVersion;

    struct nouv_dispatch_table dispatch;
    struct nouv_instance_extension_table enabled_extensions;

    int physicalDeviceCount;
    struct nouv_physical_device physicalDevices[NOUV_MAX_DRM_DEVICES];

    struct vk_debug_report_instance debug_report_callbacks;
};

struct nouv_queue {
   VK_LOADER_DATA _loader_data;
   struct nouv_device *device;
};

#define NOUV_DEFINE_HANDLE_CASTS(__nouv_type, __VkType)              \
                                                                           \
   static inline struct __nouv_type *                                   \
   __nouv_type ## _from_handle(__VkType _handle)                        \
   {                                                                       \
      return (struct __nouv_type *) _handle;                            \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __nouv_type ## _to_handle(struct __nouv_type *_obj)               \
   {                                                                       \
      return (__VkType) _obj;                                              \
   }

#define NOUV_DEFINE_NONDISP_HANDLE_CASTS(__nouv_type, __VkType)      \
                                                                           \
   static inline struct __nouv_type *                                   \
   __nouv_type ## _from_handle(__VkType _handle)                        \
   {                                                                       \
      return (struct __nouv_type *)(uintptr_t) _handle;                 \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __nouv_type ## _to_handle(struct __nouv_type *_obj)               \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define NOUV_FROM_HANDLE(__nouv_type, __name, __handle) \
   struct __nouv_type *__name = __nouv_type ## _from_handle(__handle)

NOUV_DEFINE_HANDLE_CASTS(nouv_cmd_buffer, VkCommandBuffer)
NOUV_DEFINE_HANDLE_CASTS(nouv_device, VkDevice)
NOUV_DEFINE_HANDLE_CASTS(nouv_instance, VkInstance)
NOUV_DEFINE_HANDLE_CASTS(nouv_physical_device, VkPhysicalDevice)
NOUV_DEFINE_HANDLE_CASTS(nouv_queue, VkQueue)

/* Mapping from anv object to VkDebugReportObjectTypeEXT. New types need
 * to be added here in order to utilize mapping in debug/error/perf macros.
 */
#define REPORT_OBJECT_TYPE(o)                                                      \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct nouv_instance*),             \
   VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT,                                       \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct nouv_physical_device*),      \
   VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT,                                \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct nouv_device*),               \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,                                         \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), const struct nouv_device*),         \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,                                         \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct nouv_queue*),                \
   VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct nouv_cmd_buffer*),           \
   VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT,                                 \
   /* The void expression results in a compile-time error                          \
      when assigning the result to something.  */                                  \
   (void)0))))))

#ifdef DEBUG
#define vk_error(error) __vk_errorf(NULL, NULL,\
                                    VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT,\
                                    error, __FILE__, __LINE__, NULL)
#define vk_errorf(instance, obj, error, format, ...)\
    __vk_errorf(instance, obj, REPORT_OBJECT_TYPE(obj), error,\
                __FILE__, __LINE__, format, ## __VA_ARGS__)
#else
#define vk_error(error) error
#define vk_errorf(instance, obj, error, format, ...) error
#endif


/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

VkResult
__vk_errorf(struct nouv_instance *instance, const void *object,
            VkDebugReportObjectTypeEXT type, VkResult error,
            const char *file, int line, const char *format, ...);

bool
nouv_entrypoint_is_enabled(int index, uint32_t core_version,
                              const struct nouv_instance_extension_table *,
                              const struct nouv_device_extension_table *);

int
nouv_get_entrypoint_index(const char *name);

uint32_t
nouv_physical_device_api_version(struct nouv_physical_device *);

VkResult
nouv_init_wsi(struct nouv_physical_device *);

void
nouv_finish_wsi(struct nouv_physical_device *);
