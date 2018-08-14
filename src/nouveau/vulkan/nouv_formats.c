#include "nouv_private.h"

void
nouv_GetPhysicalDeviceFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties *pFormatProperties)
{
   if (!pFormatProperties)
      return;

   pFormatProperties->linearTilingFeatures = 0;
   pFormatProperties->optimalTilingFeatures = 0;
   pFormatProperties->bufferFeatures = 0;
}

VkResult
nouv_GetPhysicalDeviceImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkImageTiling tiling,
   VkImageUsageFlags usage,
   VkImageCreateFlags createFlags,
   VkImageFormatProperties *pImageFormatProperties)
{
   return VK_SUCCESS;
}

void
nouv_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   uint32_t samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pNumProperties,
   VkSparseImageFormatProperties *pProperties)
{
   *pNumProperties = 0;
}
