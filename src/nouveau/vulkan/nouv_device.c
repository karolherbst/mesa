#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>

#include "vk_alloc.h"
#include "vk_util.h"
#include "util/macros.h"
#include "util/strtod.h"

#include "nouv_private.h"

#include "winsys/nouveau/nouveau_winsys_public.h"

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
   VkInstance instance,
   const char *pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
   VkInstance instance,
   const char *pName)
{
   return nouv_GetInstanceProcAddr(instance, pName);
}

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
   return malloc(size);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
   return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
   free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
   .pUserData = NULL,
   .pfnAllocation = default_alloc_func,
   .pfnReallocation = default_realloc_func,
   .pfnFree = default_free_func,
};

static int nouv_get_device_extension_index(const char *name);
static void nouv_physical_device_finish(struct nouv_physical_device*);
static VkResult nouv_physical_device_init(struct nouv_physical_device*,
			                     struct nouv_instance*,
			                     drmDevicePtr drm_device);
static void nouv_physical_device_init_mem_types(struct nouv_physical_device*);

VkResult
nouv_CreateDevice(
   VkPhysicalDevice physicalDevice,
   const VkDeviceCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDevice *pDevice)
{
   NOUV_FROM_HANDLE(nouv_physical_device, physical_device, physicalDevice);
   struct nouv_device *device;

   device = vk_zalloc2(&physical_device->instance->alloc, pAllocator,
                       sizeof(*device), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (!device)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   device->instance = physical_device->instance;
   if (pAllocator)
      device->alloc = *pAllocator;
   else
      device->alloc = physical_device->instance->alloc;

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      const char *ext_name = pCreateInfo->ppEnabledExtensionNames[i];
      int index = nouv_get_device_extension_index(ext_name);
      if (index < 0 || !physical_device->supported_extensions.extensions[index]) {
         vk_free(&device->alloc, device);
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);
      }

      device->enabled_extensions.extensions[index] = true;
   }

   *pDevice = nouv_device_to_handle(device);

   return VK_SUCCESS;
}

VkResult nouv_CreateInstance(
   const VkInstanceCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkInstance *pInstance)
{
   struct nouv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   struct nouv_instance_extension_table enabled_extensions = {};
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < NOUV_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    nouv_instance_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= NOUV_INSTANCE_EXTENSION_COUNT)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!nouv_instance_extensions_supported.extensions[idx])
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   instance = vk_alloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   instance->_loader_data.loaderMagic = ICD_LOADER_MAGIC;

   if (pAllocator)
      instance->alloc = *pAllocator;
   else
      instance->alloc = default_alloc;

   if (pCreateInfo->pApplicationInfo &&
       pCreateInfo->pApplicationInfo->apiVersion != 0) {
      instance->apiVersion = pCreateInfo->pApplicationInfo->apiVersion;
   } else {
      instance->apiVersion = 10000;
// TODO: 1.1
//      nouv_EnumerateInstanceVersion(&instance->apiVersion);
   }

   instance->enabled_extensions = enabled_extensions;

   for (unsigned i = 0; i < ARRAY_SIZE(instance->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!nouv_entrypoint_is_enabled(i, instance->apiVersion,
                                         &instance->enabled_extensions, NULL)) {
         instance->dispatch.entrypoints[i] = NULL;
      } else if (nouv_dispatch_table.entrypoints[i] != NULL) {
         instance->dispatch.entrypoints[i] = nouv_dispatch_table.entrypoints[i];
      } else {
         instance->dispatch.entrypoints[i] =
            nouv_tramp_dispatch_table.entrypoints[i];
      }
   }

   instance->physicalDeviceCount = -1;

   result = vk_debug_report_instance_init(&instance->debug_report_callbacks);
   if (result != VK_SUCCESS) {
      vk_free2(&default_alloc, pAllocator, instance);
      return vk_error(result);
   }

   _mesa_locale_init();

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = nouv_instance_to_handle(instance);

   return VK_SUCCESS;
}

void
nouv_DestroyDevice(
   VkDevice _device,
   const VkAllocationCallbacks* pAllocator)
{
   NOUV_FROM_HANDLE(nouv_device, device, _device);

   if (!device)
      return;

   vk_free(&device->alloc, device);
}

void
nouv_DestroyInstance(
   VkInstance _instance,
   const VkAllocationCallbacks *pAllocator)
{
   NOUV_FROM_HANDLE(nouv_instance, instance, _instance);

   if (!instance)
      return;

   for (int i = 0; i < instance->physicalDeviceCount; ++i)
      nouv_physical_device_finish(&instance->physicalDevices[i]);

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_debug_report_instance_destroy(&instance->debug_report_callbacks);

   _mesa_locale_fini();

   vk_free(&instance->alloc, instance);
}

static VkResult
nouv_enumerate_devices(struct nouv_instance *instance)
{
   drmDevicePtr devices[8];
   int max_devices;

   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;

   instance->physicalDeviceCount = 0;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   if (max_devices < 1)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x10de) {

         result = nouv_physical_device_init(&instance->physicalDevices[instance->physicalDeviceCount],
                                               instance,
                                               devices[i]);
         if (result == VK_SUCCESS)
            ++instance->physicalDeviceCount;
         else if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
            break;
      }
   }
   drmFreeDevices(devices, max_devices);

   if (result == VK_SUCCESS)
      instance->physicalDeviceCount = 1;

   return result;
}

VkResult
nouv_EnumerateDeviceExtensionProperties(
   VkPhysicalDevice physicalDevice,
   const char *pLayerName,
   uint32_t *pPropertyCount,
   VkExtensionProperties *pProperties)
{
   return VK_SUCCESS;
}

VkResult
nouv_EnumerateInstanceExtensionProperties(
   const char *pLayerName,
   uint32_t *pPropertyCount,
   VkExtensionProperties *pProperties)
{
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < NOUV_INSTANCE_EXTENSION_COUNT; i++) {
      if (nouv_instance_extensions_supported.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = nouv_instance_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult
nouv_EnumeratePhysicalDevices(
   VkInstance _instance,
   uint32_t *pPhysicalDeviceCount,
   VkPhysicalDevice *pPhysicalDevices)
{
   NOUV_FROM_HANDLE(nouv_instance, instance, _instance);
   VkResult result;

   if (instance->physicalDeviceCount < 0) {
      result = nouv_enumerate_devices(instance);
      if (result != VK_SUCCESS && result != VK_ERROR_INCOMPATIBLE_DRIVER)
         return result;
   }

   if (!pPhysicalDevices) {
      *pPhysicalDeviceCount = instance->physicalDeviceCount;
   } else {
      *pPhysicalDeviceCount = MIN2(*pPhysicalDeviceCount, instance->physicalDeviceCount);
      for (unsigned i = 0; i < *pPhysicalDeviceCount; ++i)
         pPhysicalDevices[i] = nouv_physical_device_to_handle(instance->physicalDevices + i);
   }

   return *pPhysicalDeviceCount < instance->physicalDeviceCount ? VK_INCOMPLETE : VK_SUCCESS;
}

static int
nouv_get_device_extension_index(const char *name)
{
   for (unsigned i = 0; i < NOUV_DEVICE_EXTENSION_COUNT; ++i) {
      if (strcmp(name, nouv_device_extensions[i].extensionName) == 0)
         return i;
   }
   return -1;
}

PFN_vkVoidFunction
nouv_GetInstanceProcAddr(
   VkInstance _instance,
   const char *pName
) {
   NOUV_FROM_HANDLE(nouv_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_NOUV_ENTRYPOINT(entrypoint) \
   if (strcmp(pName, "vk" #entrypoint) == 0) \
      return (PFN_vkVoidFunction)nouv_##entrypoint

   LOOKUP_NOUV_ENTRYPOINT(CreateInstance);
   LOOKUP_NOUV_ENTRYPOINT(EnumerateInstanceExtensionProperties);
//   LOOKUP_NOUV_ENTRYPOINT(EnumerateInstanceLayerProperties);
//   LOOKUP_NOUV_ENTRYPOINT(EnumerateInstanceVersion);

#undef LOOKUP_NOUV_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   int idx = nouv_get_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return instance->dispatch.entrypoints[idx];
}

void
nouv_GetPhysicalDeviceFeatures(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceFeatures *pFeatures)
{
}

void
nouv_GetPhysicalDeviceMemoryProperties(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
   NOUV_FROM_HANDLE(nouv_physical_device, physical_device, physicalDevice);
   *pMemoryProperties = physical_device->memory_properties;
}

PFN_vkVoidFunction
nouv_GetDeviceProcAddr(
   VkDevice _device,
   const char *pName)
{
   NOUV_FROM_HANDLE(nouv_device, device, _device);

   if (!device || !pName)
      return NULL;

   int idx = nouv_get_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   if (!nouv_entrypoint_is_enabled(idx, device->instance->apiVersion,
                                      &device->instance->enabled_extensions,
                                      &device->enabled_extensions))
      return NULL;

   return nouv_dispatch_table.entrypoints[idx];
}

void
nouv_GetPhysicalDeviceProperties(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceProperties *pProperties)
{
   NOUV_FROM_HANDLE(nouv_physical_device, pdevice, physicalDevice);

   VkPhysicalDeviceLimits limits = {
      .maxFragmentInputComponents = 124,
      .maxGeometryInputComponents = 128,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 1024,
      .maxGeometryTotalOutputComponents = 1024,
      .maxImageArrayLayers = 1 << 11,
      .maxImageDimension1D = 1 << 14,
      .maxImageDimension2D = 1 << 14,
      .maxImageDimension3D = 1 << 11,
      .maxImageDimensionCube = 1 << 14,
      .maxVertexOutputComponents = 128,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = nouv_physical_device_api_version(pdevice),
      .driverVersion = vk_get_driver_version(),
      .vendorID = 0x10de,
      .deviceID = pdevice->info.device_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
      .limits = limits,
      .sparseProperties = {0},
   };

   strcpy(pProperties->deviceName, pdevice->info.name);
}

static void
nouv_get_physical_device_queue_family_properties(
   struct nouv_physical_device *pdevice,
   uint32_t *pCount,
   VkQueueFamilyProperties **pQueueFamilyProperties)
{
   if (pQueueFamilyProperties == NULL) {
      *pCount = 1;
      return;
   }

   assert(*pCount == 1);

   *pQueueFamilyProperties[0] = (VkQueueFamilyProperties) {
      .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
      .queueCount = 1,
      .timestampValidBits = 32,
      .minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
   };
}

void
nouv_GetPhysicalDeviceQueueFamilyProperties(
   VkPhysicalDevice physicalDevice,
   uint32_t *pCount,
   VkQueueFamilyProperties *pQueueFamilyProperties)
{
   NOUV_FROM_HANDLE(nouv_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      nouv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
      return;
   }

   VkQueueFamilyProperties *properties[] = {
      &pQueueFamilyProperties[0],
   };
   nouv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
}

static void
nouv_physical_device_finish(struct nouv_physical_device *device)
{
   nouv_finish_wsi(device);
   device->ws->destroy(device->ws);
   close(device->local_fd);
}

static VkResult
nouv_physical_device_init(struct nouv_physical_device *device,
                          struct nouv_instance *instance,
                          drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   drmVersionPtr version;
   VkResult result;
   uint64_t val;
   int fd;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return vk_errorf(instance, device, VK_ERROR_INCOMPATIBLE_DRIVER, "failed to get version %s: %m", path);
   }
   drmFreeVersion(version);

   device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   device->instance = instance;
   assert(strlen(path) < ARRAY_SIZE(device->path));
   strncpy(device->path, path, ARRAY_SIZE(device->path));
   device->ws = nouv_nouveau_winsys_create(fd);

   if (!device->ws) {
      result = vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);
      goto fail;
   }

   device->local_fd = fd;

#define GET_INFO(p, d)                                  \
   if (!device->ws->get_param(device->ws, (p), &val)) { \
      result = vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);  \
      goto fail;                                        \
   }                                                    \
   (d) = val

   GET_INFO(NOUV_WINSYS_PARAM_CHIPSET, device->info.chipset);
   GET_INFO(NOUV_WINSYS_PARAM_GART_SIZE, device->info.gart_size);
   GET_INFO(NOUV_WINSYS_PARAM_PCI_DEVICE, device->info.device_id);
   GET_INFO(NOUV_WINSYS_PARAM_VRAM_SIZE, device->info.vram_size);
#undef GET_INFO
   snprintf(device->info.name, ARRAY_SIZE(device->info.name), "NV%x", device->info.chipset);

   nouv_physical_device_init_mem_types(device);

   result = nouv_init_wsi(device);
   if (result != VK_SUCCESS) {
      device->ws->destroy(device->ws);
      vk_error(result);
      goto fail;
   }

   return VK_SUCCESS;

fail:
   close(fd);
   return result;
}

static void
nouv_physical_device_init_mem_types(struct nouv_physical_device *device)
{
   STATIC_ASSERT(NOUV_MEM_HEAP_COUNT <= VK_MAX_MEMORY_HEAPS);
   STATIC_ASSERT(NOUV_MEM_TYPE_COUNT <= VK_MAX_MEMORY_TYPES);

   int gart_index = -1;
   int vram_index = -1;

   device->memory_properties.memoryHeapCount = 0;
   if (device->info.gart_size > 0) {
      gart_index = device->memory_properties.memoryHeapCount++;
      device->memory_properties.memoryHeaps[gart_index] = (VkMemoryHeap) {
         .size = device->info.gart_size,
         .flags = 0,
      };
   }

   if (device->info.vram_size > 0) {
      vram_index = device->memory_properties.memoryHeapCount++;
      device->memory_properties.memoryHeaps[vram_index] = (VkMemoryHeap) {
         .size = device->info.vram_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
   }

   unsigned type_count = 0;
   if (gart_index >= 0) {
      device->mem_type_indices[type_count] = NOUV_MEM_TYPE_GART;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
         .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         .heapIndex = gart_index,
      };
   }

   if (vram_index >= 0) {
      device->mem_type_indices[type_count] = NOUV_MEM_TYPE_VRAM;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = vram_index,
      };
   }
   device->memory_properties.memoryTypeCount = type_count;
}
