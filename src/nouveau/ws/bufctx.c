#include "priv.h"

int
nouveau_ws_bufctx_new(struct nouveau_ws_client *client,
                      int bins,
                      struct nouveau_ws_bufctx **bctx)
{
   assert(client && bctx);

   struct nouveau_ws_bufctx_priv *priv = CALLOC_STRUCT(nouveau_ws_bufctx_priv);
   if (!priv)
      return -ENOMEM;

   list_inithead(&priv->base.current);
   list_inithead(&priv->base.pending);
   *bctx = &priv->base;
   return 0;
}

void
nouveau_ws_bufctx_del(struct nouveau_ws_bufctx **bctx)
{
   if (!bctx)
      return;

   FREE(*bctx);
   *bctx = NULL;
}

struct nouveau_ws_bufref *
nouveau_ws_bufctx_refn(struct nouveau_ws_bufctx *bctx,
                       int bin,
                       struct nouveau_ws_bo *bo,
                       uint32_t flags)
{
   assert(bctx && bo);

   struct nouveau_ws_bufref_priv *priv = CALLOC_STRUCT(nouveau_ws_bufref_priv);

   if (priv) {
      priv->base.flags = flags;
      priv->bo = bo;
      list_add(&priv->base.thead, &bctx->pending);
   }

   return &priv->base;
}

struct nouveau_ws_bufref *
nouveau_ws_bufctx_mthd(struct nouveau_ws_bufctx *bctx,
                       int bin,
                       uint32_t packet,
                       struct nouveau_ws_bo *bo,
                       uint64_t data,
                       uint32_t flags,
                       uint32_t vor,
                       uint32_t tor)
{
   assert(false);
   return NULL;
}

void
nouveau_ws_bufctx_reset(struct nouveau_ws_bufctx *bctx,
                        int bin)
{
}
