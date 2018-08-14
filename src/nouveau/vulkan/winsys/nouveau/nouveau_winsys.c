#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <libdrm/nouveau_drm.h>
#include <libdrm/nouveau/nouveau.h>
#include <libdrm/nouveau/nvif/class.h>
#include <libdrm/nouveau/nvif/cl0080.h>

#include "nouveau_winsys_public.h"

#define NOUV_NOUVEAU_WS(ws) ((struct nouv_nouveau_winsys*)ws)
#define NOUV_WS(ws) ((struct nouv_winsys*)ws)

struct nouv_nouveau_winsys {
   struct nouv_winsys base;
   struct nouveau_device *dev;
   struct nouveau_drm *drm;
};

static void
nouv_winsys_destroy(struct nouv_winsys *ws)
{
   struct nouv_nouveau_winsys *nws = NOUV_NOUVEAU_WS(ws);
   nouveau_device_del(&nws->dev);
   nouveau_drm_del(&nws->drm);
   free(ws);
}

static bool
nouv_winsys_get_param(
   struct nouv_winsys *ws,
   enum nouv_winsys_param param,
   uint64_t *val)
{
   if (!val)
      return false;

   struct nouv_nouveau_winsys *nws = NOUV_NOUVEAU_WS(ws);

   uint64_t ws_param;
   switch (param) {
#define CASE_MAP(a, b) \
   case a:             \
      ws_param = b;    \
      break
#define CASE_VAL(a, b) \
   case a:             \
      *val = (b);      \
      return true
   CASE_MAP(NOUV_WINSYS_PARAM_CHIPSET, NOUVEAU_GETPARAM_CHIPSET_ID);
   CASE_VAL(NOUV_WINSYS_PARAM_GART_SIZE, nws->dev->gart_size);
   CASE_MAP(NOUV_WINSYS_PARAM_PCI_DEVICE, NOUVEAU_GETPARAM_PCI_DEVICE);
   CASE_VAL(NOUV_WINSYS_PARAM_VRAM_SIZE, nws->dev->vram_size);
#undef CASE_MAP
#undef CASE_VAL
   }

   return !nouveau_getparam(nws->dev, ws_param, val);
}

struct nouv_winsys*
nouv_nouveau_winsys_create(int fd)
{
   struct nouv_nouveau_winsys *ws;
   int dupfd;
   int ret;

   ws = calloc(1, sizeof(struct nouv_nouveau_winsys));

   if (!ws)
      return NULL;

   ws->base.destroy = nouv_winsys_destroy;
   ws->base.get_param = nouv_winsys_get_param;

   dupfd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
   ret = nouveau_drm_new(dupfd, &ws->drm);
   if (ret)
      goto err;

   ret = nouveau_device_new(&ws->drm->client, NV_DEVICE, &(struct nv_device_v0) { .device = ~0ULL, }, sizeof(struct nv_device_v0), &ws->dev);
   if (ret)
      goto err;

   return NOUV_WS(ws);

err:
   nouveau_device_del(&ws->dev);
   nouveau_drm_del(&ws->drm);
   close(dupfd);
   return NULL;
}
