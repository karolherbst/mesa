/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 *
 */

#include "draw/draw_context.h"
#include "util/u_upload_mgr.h"

#include "nv_m2mf.xml.h"
#include "nv_object.xml.h"
#include "nv30/nv01_2d.xml.h"
#include "nv30/nv30-40_3d.xml.h"

#include "nouveau_fence.h"
#include "nv30/nv30_context.h"
#include "nv30/nv30_transfer.h"
#include "nv30/nv30_state.h"

static void
nv30_context_kick_notify(struct nouveau_pushbuf *push)
{
   struct nv30_context *nv30;
   struct nouveau_context *context;

   if (!push->user_priv)
      return;
   nv30 = container_of(push->user_priv, nv30, bufctx);
   context = &nv30->base;

   nouveau_fence_next(context);
   nouveau_fence_update(context, true);

   if (push->bufctx) {
      struct nouveau_bufref *bref;
      LIST_FOR_EACH_ENTRY(bref, &push->bufctx->current, thead) {
         struct nv04_resource *res = bref->priv;
         if (res && res->mm) {
            nouveau_fence_ref(context->fence.current, &res->fence);

            if (bref->flags & NOUVEAU_BO_RD)
               res->status |= NOUVEAU_BUFFER_STATUS_GPU_READING;

            if (bref->flags & NOUVEAU_BO_WR) {
               nouveau_fence_ref(context->fence.current, &res->fence_wr);
               res->status |= NOUVEAU_BUFFER_STATUS_GPU_WRITING |
                  NOUVEAU_BUFFER_STATUS_DIRTY;
            }
         }
      }
   }
}

static void
nv30_context_flush(struct pipe_context *pipe, struct pipe_fence_handle **fence,
                   unsigned flags)
{
   struct nv30_context *nv30 = nv30_context(pipe);
   struct nouveau_pushbuf *push = nv30->base.pushbuf;

   if (fence)
      nouveau_fence_ref(nv30->base.fence.current,
                        (struct nouveau_fence **)fence);

   PUSH_KICK(push);

   nouveau_context_update_frame_stats(&nv30->base);
}

static int
nv30_invalidate_resource_storage(struct nouveau_context *nv,
                                 struct pipe_resource *res,
                                 int ref)
{
   struct nv30_context *nv30 = nv30_context(&nv->pipe);
   unsigned i;

   if (res->bind & PIPE_BIND_RENDER_TARGET) {
      for (i = 0; i < nv30->framebuffer.nr_cbufs; ++i) {
         if (nv30->framebuffer.cbufs[i] &&
             nv30->framebuffer.cbufs[i]->texture == res) {
            nv30->dirty |= NV30_NEW_FRAMEBUFFER;
            nouveau_bufctx_reset(nv30->bufctx, BUFCTX_FB);
            if (!--ref)
               return ref;
         }
      }
   }
   if (res->bind & PIPE_BIND_DEPTH_STENCIL) {
      if (nv30->framebuffer.zsbuf &&
          nv30->framebuffer.zsbuf->texture == res) {
            nv30->dirty |= NV30_NEW_FRAMEBUFFER;
            nouveau_bufctx_reset(nv30->bufctx, BUFCTX_FB);
            if (!--ref)
               return ref;
      }
   }

   if (res->bind & PIPE_BIND_VERTEX_BUFFER) {
      for (i = 0; i < nv30->num_vtxbufs; ++i) {
         if (nv30->vtxbuf[i].buffer.resource == res) {
            nv30->dirty |= NV30_NEW_ARRAYS;
            nouveau_bufctx_reset(nv30->bufctx, BUFCTX_VTXBUF);
            if (!--ref)
               return ref;
         }
      }
   }

   if (res->bind & PIPE_BIND_SAMPLER_VIEW) {
      for (i = 0; i < nv30->fragprog.num_textures; ++i) {
         if (nv30->fragprog.textures[i] &&
             nv30->fragprog.textures[i]->texture == res) {
            nv30->dirty |= NV30_NEW_FRAGTEX;
            nouveau_bufctx_reset(nv30->bufctx, BUFCTX_FRAGTEX(i));
            if (!--ref)
               return ref;
         }
      }
      for (i = 0; i < nv30->vertprog.num_textures; ++i) {
         if (nv30->vertprog.textures[i] &&
             nv30->vertprog.textures[i]->texture == res) {
            nv30->dirty |= NV30_NEW_VERTTEX;
            nouveau_bufctx_reset(nv30->bufctx, BUFCTX_VERTTEX(i));
            if (!--ref)
               return ref;
         }
      }
   }

   return ref;
}

static void
nv30_context_fence_emit(struct pipe_context *pcontext, uint32_t *sequence)
{
   struct nv30_context *context = nv30_context(pcontext);
   struct nouveau_pushbuf *push = context->base.pushbuf;

   *sequence = ++context->base.fence.sequence;

   assert(PUSH_AVAIL(push) + push->rsvd_kick >= 3);
   PUSH_DATA (push, NV30_3D_FENCE_OFFSET |
              (2 /* size */ << 18) | (7 /* subchan */ << 13));
   PUSH_DATA (push, 0);
   PUSH_DATA (push, *sequence);
}

static uint32_t
nv30_context_fence_update(struct pipe_context *pcontext)
{
   struct nv30_context *context = nv30_context(pcontext);
   struct nv04_notify *fence = context->fence->data;
   return *(uint32_t *)((char *)context->notify->map + fence->offset);
}

static void
nv30_context_destroy(struct pipe_context *pipe)
{
   struct nv30_context *nv30 = nv30_context(pipe);

   if (nv30->blitter)
      util_blitter_destroy(nv30->blitter);

   if (nv30->draw)
      draw_destroy(nv30->draw);

   if (nv30->base.pipe.stream_uploader)
      u_upload_destroy(nv30->base.pipe.stream_uploader);

   if (nv30->blit_vp)
      nouveau_heap_free(&nv30->blit_vp);

   if (nv30->blit_fp)
      pipe_resource_reference(&nv30->blit_fp, NULL);

   if (nv30->base.pushbuf->user_priv == &nv30->bufctx)
      nv30->base.pushbuf->user_priv = NULL;

   nouveau_bufctx_del(&nv30->bufctx);

   if (nv30->screen->cur_ctx == nv30)
      nv30->screen->cur_ctx = NULL;

   if (nv30->base.fence.current) {
      struct nouveau_fence *current = NULL;

      /* nouveau_fence_wait will create a new current fence, so wait on the
       * _current_ one, and remove both.
       */
      nouveau_fence_ref(nv30->base.fence.current, &current);
      nouveau_fence_wait(current, NULL);
      nouveau_fence_ref(NULL, &current);
      nouveau_fence_ref(NULL, &nv30->base.fence.current);
   }

   nouveau_bo_ref(NULL, &nv30->notify);
   nouveau_heap_destroy(&nv30->query_heap);

   nouveau_object_del(&nv30->query);
   nouveau_object_del(&nv30->fence);

   nouveau_heap_destroy(&nv30->vp_exec_heap);
   nouveau_heap_destroy(&nv30->vp_data_heap);

   nouveau_object_del(&nv30->ntfy);

   nouveau_object_del(&nv30->sifm);
   nouveau_object_del(&nv30->swzsurf);
   nouveau_object_del(&nv30->surf2d);
   nouveau_object_del(&nv30->m2mf);
   nouveau_object_del(&nv30->eng3d);
   nouveau_object_del(&nv30->null);

   nouveau_context_destroy(&nv30->base);
}

#define FAIL_CONTEXT_INIT(str, err)                   \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      nv30_context_destroy(pipe);                     \
      return NULL;                                    \
   } while(0)

struct pipe_context *
nv30_context_create(struct pipe_screen *pscreen, void *priv, unsigned ctxflags)
{
   struct nv30_screen *screen = nv30_screen(pscreen);
   struct nv30_context *nv30 = CALLOC_STRUCT(nv30_context);
   struct nv04_fifo *fifo;
   struct nouveau_pushbuf *push;
   struct pipe_context *pipe;
   unsigned oclass = 0;
   int ret, i;

   if (!nv30)
      return NULL;

   nv30->screen = screen;
   nv30->base.screen = &screen->base;
   nv30->base.copy_data = nv30_transfer_copy_data;

   pipe = &nv30->base.pipe;
   pipe->screen = pscreen;
   pipe->priv = priv;
   pipe->destroy = nv30_context_destroy;
   pipe->flush = nv30_context_flush;

   fifo = nv30->base.channel->data;
   push = nv30->base.pushbuf;
   push->rsvd_kick = 16;

   ret = nouveau_object_new(nv30->base.channel, 0x00000000, NV01_NULL_CLASS,
                            NULL, 0, &nv30->null);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating null object: %d\n", ret);

   /* DMA_NOTIFY object, we don't actually use this but M2MF fails without */
   ret = nouveau_object_new(nv30->base.channel, 0xbeef0301,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 32 }, sizeof(struct nv04_notify),
                            &nv30->ntfy);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating sync notifier: %d\n", ret);

   /* Vertex program resources (code/data), currently 6 of the constant
    * slots are reserved to implement user clipping planes
    */
   if (oclass < NV40_3D_CLASS) {
      nouveau_heap_init(&nv30->vp_exec_heap, 0, 256);
      nouveau_heap_init(&nv30->vp_data_heap, 6, 256 - 6);
   } else {
      nouveau_heap_init(&nv30->vp_exec_heap, 0, 512);
      nouveau_heap_init(&nv30->vp_data_heap, 6, 468 - 6);
   }

   ret = nouveau_object_new(nv30->base.channel, 0xbeef3097, oclass,
                            NULL, 0, &nv30->eng3d);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating 3d object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
   PUSH_DATA (push, nv30->eng3d->handle);
   BEGIN_NV04(push, NV30_3D(DMA_NOTIFY), 13);
   PUSH_DATA (push, nv30->ntfy->handle);
   PUSH_DATA (push, fifo->vram);     /* TEXTURE0 */
   PUSH_DATA (push, fifo->gart);     /* TEXTURE1 */
   PUSH_DATA (push, fifo->vram);     /* COLOR1 */
   PUSH_DATA (push, nv30->null->handle);  /* UNK190 */
   PUSH_DATA (push, fifo->vram);     /* COLOR0 */
   PUSH_DATA (push, fifo->vram);     /* ZETA */
   PUSH_DATA (push, fifo->vram);     /* VTXBUF0 */
   PUSH_DATA (push, fifo->gart);     /* VTXBUF1 */
   PUSH_DATA (push, nv30->fence->handle);  /* FENCE */
   PUSH_DATA (push, nv30->query->handle);  /* QUERY - intr 0x80 if nullobj */
   PUSH_DATA (push, nv30->null->handle);  /* UNK1AC */
   PUSH_DATA (push, nv30->null->handle);  /* UNK1B0 */
   if (nv30->eng3d->oclass < NV40_3D_CLASS) {
      BEGIN_NV04(push, SUBC_3D(0x03b0), 1);
      PUSH_DATA (push, 0x00100000);
      BEGIN_NV04(push, SUBC_3D(0x1d80), 1);
      PUSH_DATA (push, 3);

      BEGIN_NV04(push, SUBC_3D(0x1e98), 1);
      PUSH_DATA (push, 0);
      BEGIN_NV04(push, SUBC_3D(0x17e0), 3);
      PUSH_DATA (push, fui(0.0));
      PUSH_DATA (push, fui(0.0));
      PUSH_DATA (push, fui(1.0));
      BEGIN_NV04(push, SUBC_3D(0x1f80), 16);
      for (i = 0; i < 16; i++)
         PUSH_DATA (push, (i == 8) ? 0x0000ffff : 0);

      BEGIN_NV04(push, NV30_3D(RC_ENABLE), 1);
      PUSH_DATA (push, 0);
   } else {
      BEGIN_NV04(push, NV40_3D(DMA_COLOR2), 2);
      PUSH_DATA (push, fifo->vram);
      PUSH_DATA (push, fifo->vram);  /* COLOR3 */

      BEGIN_NV04(push, SUBC_3D(0x1450), 1);
      PUSH_DATA (push, 0x00000004);

      BEGIN_NV04(push, SUBC_3D(0x1ea4), 3); /* ZCULL */
      PUSH_DATA (push, 0x00000010);
      PUSH_DATA (push, 0x01000100);
      PUSH_DATA (push, 0xff800006);

      /* vtxprog output routing */
      BEGIN_NV04(push, SUBC_3D(0x1fc4), 1);
      PUSH_DATA (push, 0x06144321);
      BEGIN_NV04(push, SUBC_3D(0x1fc8), 2);
      PUSH_DATA (push, 0xedcba987);
      PUSH_DATA (push, 0x0000006f);
      BEGIN_NV04(push, SUBC_3D(0x1fd0), 1);
      PUSH_DATA (push, 0x00171615);
      BEGIN_NV04(push, SUBC_3D(0x1fd4), 1);
      PUSH_DATA (push, 0x001b1a19);

      BEGIN_NV04(push, SUBC_3D(0x1ef8), 1);
      PUSH_DATA (push, 0x0020ffff);
      BEGIN_NV04(push, SUBC_3D(0x1d64), 1);
      PUSH_DATA (push, 0x01d300d4);

      BEGIN_NV04(push, NV40_3D(MIPMAP_ROUNDING), 1);
      PUSH_DATA (push, NV40_3D_MIPMAP_ROUNDING_MODE_DOWN);
   }

   ret = nouveau_object_new(nv30->base.channel, 0xbeef3901, NV03_M2MF_CLASS,
                            NULL, 0, &nv30->m2mf);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating m2mf object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(M2MF, OBJECT), 1);
   PUSH_DATA (push, nv30->m2mf->handle);
   BEGIN_NV04(push, NV03_M2MF(DMA_NOTIFY), 1);
   PUSH_DATA (push, nv30->ntfy->handle);

   ret = nouveau_object_new(nv30->base.channel, 0xbeef6201,
                            NV10_SURFACE_2D_CLASS, NULL, 0, &nv30->surf2d);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating surf2d object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SF2D, OBJECT), 1);
   PUSH_DATA (push, nv30->surf2d->handle);
   BEGIN_NV04(push, NV04_SF2D(DMA_NOTIFY), 1);
   PUSH_DATA (push, nv30->ntfy->handle);

   if (screen->base.device->chipset < 0x40)
      oclass = NV30_SURFACE_SWZ_CLASS;
   else
      oclass = NV40_SURFACE_SWZ_CLASS;

   ret = nouveau_object_new(nv30->base.channel, 0xbeef5201, oclass,
                            NULL, 0, &nv30->swzsurf);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating swizzled surface object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SSWZ, OBJECT), 1);
   PUSH_DATA (push, nv30->swzsurf->handle);
   BEGIN_NV04(push, NV04_SSWZ(DMA_NOTIFY), 1);
   PUSH_DATA (push, nv30->ntfy->handle);

   if (screen->base.device->chipset < 0x40)
      oclass = NV30_SIFM_CLASS;
   else
      oclass = NV40_SIFM_CLASS;

   ret = nouveau_object_new(nv30->base.channel, 0xbeef7701, oclass,
                            NULL, 0, &nv30->sifm);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating scaled image object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SIFM, OBJECT), 1);
   PUSH_DATA (push, nv30->sifm->handle);
   BEGIN_NV04(push, NV03_SIFM(DMA_NOTIFY), 1);
   PUSH_DATA (push, nv30->ntfy->handle);
   BEGIN_NV04(push, NV05_SIFM(COLOR_CONVERSION), 1);
   PUSH_DATA (push, NV05_SIFM_COLOR_CONVERSION_TRUNCATE);

   nouveau_pushbuf_kick(push, push->channel);

   nv30->base.pipe.stream_uploader = u_upload_create_default(&nv30->base.pipe);
   if (!nv30->base.pipe.stream_uploader) {
      nv30_context_destroy(pipe);
      return NULL;
   }
   nv30->base.pipe.const_uploader = nv30->base.pipe.stream_uploader;

   /*XXX: *cough* per-context client */
   nv30->base.client = screen->base.client;

   /*XXX: *cough* per-context pushbufs */
   push = nv30->base.pushbuf;
   nv30->base.pushbuf = push;
   nv30->base.pushbuf->user_priv = &nv30->bufctx; /* hack at validate time */
   nv30->base.pushbuf->rsvd_kick = 16; /* hack in screen before first space */
   nv30->base.pushbuf->kick_notify = nv30_context_kick_notify;
   nv30->base.fence.emit = nv30_context_fence_emit;
   nv30->base.fence.update = nv30_context_fence_update;

   nv30->base.invalidate_resource_storage = nv30_invalidate_resource_storage;

   /* DMA_FENCE refuses to accept DMA objects with "adjust" filled in,
    * this means that the address pointed at by the DMA object must
    * be 4KiB aligned, which means this object needs to be the first
    * one allocated on the channel.
    */
   ret = nouveau_object_new(nv30->base.channel, 0xbeef1e00,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 32 }, sizeof(struct nv04_notify),
                            &nv30->fence);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating fence notifier: %d\n", ret);

   /* DMA_QUERY, used to implement occlusion queries, we attempt to allocate
    * the remainder of the "notifier block" assigned by the kernel for
    * use as query objects
    */
   ret = nouveau_object_new(nv30->base.channel, 0xbeef0351,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 4096 - 128 }, sizeof(struct nv04_notify),
                            &nv30->query);
   if (ret)
      FAIL_CONTEXT_INIT("error allocating query notifier: %d\n", ret);

   ret = nouveau_heap_init(&nv30->query_heap, 0, 4096 - 128);
   if (ret)
      FAIL_CONTEXT_INIT("error creating query heap: %d\n", ret);

   LIST_INITHEAD(&nv30->queries);

   fifo = nv30->base.channel->data;
   ret = nouveau_bo_wrap(screen->base.device, fifo->notify, &nv30->notify);
   if (ret == 0)
      ret = nouveau_bo_map(nv30->notify, 0, nv30->base.client);
   if (ret)
      FAIL_CONTEXT_INIT("error mapping notifier memory: %d\n", ret);

   nouveau_fence_new(&nv30->base, &nv30->base.fence.current);

   ret = nouveau_bufctx_new(nv30->base.client, 64, &nv30->bufctx);
   if (ret) {
      nv30_context_destroy(pipe);
      return NULL;
   }

   /*XXX: make configurable with performance vs quality, these defaults
    *     match the binary driver's defaults
    */
   if (nv30->eng3d->oclass < NV40_3D_CLASS)
      nv30->config.filter = 0x00000004;
   else
      nv30->config.filter = 0x00002dc4;

   nv30->config.aniso = NV40_3D_TEX_WRAP_ANISO_MIP_FILTER_OPTIMIZATION_OFF;

   if (debug_get_bool_option("NV30_SWTNL", false))
      nv30->draw_flags |= NV30_NEW_SWTNL;

   nouveau_context_init(&nv30->base);
   nv30->sample_mask = 0xffff;
   nv30_vbo_init(pipe);
   nv30_query_init(pipe);
   nv30_state_init(pipe);
   nv30_resource_init(pipe);
   nv30_clear_init(pipe);
   nv30_fragprog_init(pipe);
   nv30_vertprog_init(pipe);
   nv30_texture_init(pipe);
   nv30_fragtex_init(pipe);
   nv40_verttex_init(pipe);
   nv30_draw_init(pipe);

   nv30->blitter = util_blitter_create(pipe);
   if (!nv30->blitter) {
      nv30_context_destroy(pipe);
      return NULL;
   }

   nouveau_context_init_vdec(&nv30->base);

   return pipe;
}
