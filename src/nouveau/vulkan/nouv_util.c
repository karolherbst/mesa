#include <stdarg.h>
#include <stdio.h>

#include "vk_enum_to_str.h"

#include "util/debug.h"

#include "nouv_private.h"

VkResult
__vk_errorf(struct nouv_instance *instance, const void *object,
            VkDebugReportObjectTypeEXT type, VkResult error,
            const char *file, int line, const char *format, ...)
{
   va_list ap;
   char buffer[256];
   char report[512];

   const char *error_str = vk_Result_to_str(error);

   if (format) {
      va_start(ap, format);
      vsnprintf(buffer, sizeof(buffer), format, ap);
      va_end(ap);

      snprintf(report, sizeof(report), "%s:%d: %s (%s)", file, line, buffer,
               error_str);
   } else {
      snprintf(report, sizeof(report), "%s:%d: %s", file, line, error_str);
   }

   if (instance) {
      vk_debug_report(&instance->debug_report_callbacks,
                      VK_DEBUG_REPORT_ERROR_BIT_EXT,
                      type,
                      (uint64_t) (uintptr_t) object,
                      line,
                      0,
                      "anv",
                      report);
   }

   if (error == VK_ERROR_DEVICE_LOST &&
       env_var_as_boolean("NOUV_ABORT_ON_DEVICE_LOSS", false))
      abort();

   return error;
}
