
#include "target-helpers/inline_debug_helper.h"
#include "frontend/drm_driver.h"
#include "iris/drm/iris_drm_public.h"
#include "util/driconf.h"

static struct pipe_screen *
create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = iris_drm_screen_create(fd, config);
   if (!screen)
      return NULL;

   screen = debug_screen_wrap(screen);

   return screen;
}

static const char *driconf_xml =
   #include "iris/iris_driinfo.h"
   ;

PUBLIC
DRM_DRIVER_DESCRIPTOR("iris", &driconf_xml, create_screen)
