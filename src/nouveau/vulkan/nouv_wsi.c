#include "wsi_common.h"

#include "nouv_private.h"

static PFN_vkVoidFunction
nouv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   int idx = nouv_get_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return nouv_dispatch_table.entrypoints[idx];
}

void
nouv_finish_wsi(struct nouv_physical_device *physical_device)
{
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->alloc);
}

VkResult
nouv_init_wsi(struct nouv_physical_device *physical_device)
{
   return wsi_device_init(&physical_device->wsi_device,
                          nouv_physical_device_to_handle(physical_device),
                          nouv_wsi_proc_addr,
                          &physical_device->instance->alloc,
                          physical_device->master_fd);
}
