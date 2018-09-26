/*
 * Copyright 2010 Christoph Bumiller
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
 */

#include "pipe/p_defines.h"
#include "util/u_framebuffer.h"
#include "util/u_upload_mgr.h"

#include <nvif/class.h>

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_screen.h"
#include "nvc0/nvc0_resource.h"

#include "nvc0/mme/com9097.mme.h"
#include "nvc0/mme/com90c0.mme.h"

#include "nv50/g80_texture.xml.h"

static void
nvc0_flush(struct pipe_context *pipe,
           struct pipe_fence_handle **fence,
           unsigned flags)
{
   struct nvc0_context *nvc0 = nvc0_context(pipe);

   if (fence)
      nouveau_fence_ref(nvc0->base.fence.current, (struct nouveau_fence **)fence);

   PUSH_KICK(nvc0->base.pushbuf); /* fencing handled in kick_notify */

   nouveau_context_update_frame_stats(&nvc0->base);
}

static void
nvc0_texture_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct nouveau_pushbuf *push = nvc0_context(pipe)->base.pushbuf;

   IMMED_NVC0(push, NVC0_3D(SERIALIZE), 0);
   IMMED_NVC0(push, NVC0_3D(TEX_CACHE_CTL), 0);
}

static void
nvc0_memory_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct nvc0_context *nvc0 = nvc0_context(pipe);
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   int i, s;

   if (flags & PIPE_BARRIER_MAPPED_BUFFER) {
      for (i = 0; i < nvc0->num_vtxbufs; ++i) {
         if (!nvc0->vtxbuf[i].buffer.resource && !nvc0->vtxbuf[i].is_user_buffer)
            continue;
         if (nvc0->vtxbuf[i].buffer.resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
            nvc0->base.vbo_dirty = true;
      }

      for (s = 0; s < 5 && !nvc0->cb_dirty; ++s) {
         uint32_t valid = nvc0->constbuf_valid[s];

         while (valid && !nvc0->cb_dirty) {
            const unsigned i = ffs(valid) - 1;
            struct pipe_resource *res;

            valid &= ~(1 << i);
            if (nvc0->constbuf[s][i].user)
               continue;

            res = nvc0->constbuf[s][i].u.buf;
            if (!res)
               continue;

            if (res->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
               nvc0->cb_dirty = true;
         }
      }
   } else {
      /* Pretty much any writing by shaders needs a serialize after
       * it. Especially when moving between 3d and compute pipelines, but even
       * without that.
       */
      IMMED_NVC0(push, NVC0_3D(SERIALIZE), 0);
   }

   /* If we're going to texture from a buffer/image written by a shader, we
    * must flush the texture cache.
    */
   if (flags & PIPE_BARRIER_TEXTURE)
      IMMED_NVC0(push, NVC0_3D(TEX_CACHE_CTL), 0);

   if (flags & PIPE_BARRIER_CONSTANT_BUFFER)
      nvc0->cb_dirty = true;
   if (flags & (PIPE_BARRIER_VERTEX_BUFFER | PIPE_BARRIER_INDEX_BUFFER))
      nvc0->base.vbo_dirty = true;
}

static void
nvc0_emit_string_marker(struct pipe_context *pipe, const char *str, int len)
{
   struct nouveau_pushbuf *push = nvc0_context(pipe)->base.pushbuf;
   int string_words = len / 4;
   int data_words;

   if (len <= 0)
      return;
   string_words = MIN2(string_words, NV04_PFIFO_MAX_PACKET_LEN);
   if (string_words == NV04_PFIFO_MAX_PACKET_LEN)
      data_words = string_words;
   else
      data_words = string_words + !!(len & 3);
   BEGIN_NIC0(push, SUBC_3D(NV04_GRAPH_NOP), data_words);
   if (string_words)
      PUSH_DATAp(push, str, string_words);
   if (string_words != data_words) {
      int data = 0;
      memcpy(&data, &str[string_words * 4], len & 3);
      PUSH_DATA (push, data);
   }
}

static void
nvc0_context_unreference_resources(struct nvc0_context *nvc0)
{
   unsigned s, i;

   nouveau_bufctx_del(&nvc0->bufctx_3d);
   nouveau_bufctx_del(&nvc0->bufctx);
   nouveau_bufctx_del(&nvc0->bufctx_cp);

   util_unreference_framebuffer_state(&nvc0->framebuffer);

   for (i = 0; i < nvc0->num_vtxbufs; ++i)
      pipe_vertex_buffer_unreference(&nvc0->vtxbuf[i]);

   for (s = 0; s < 6; ++s) {
      for (i = 0; i < nvc0->num_textures[s]; ++i)
         pipe_sampler_view_reference(&nvc0->textures[s][i], NULL);

      for (i = 0; i < NVC0_MAX_PIPE_CONSTBUFS; ++i)
         if (!nvc0->constbuf[s][i].user)
            pipe_resource_reference(&nvc0->constbuf[s][i].u.buf, NULL);

      for (i = 0; i < NVC0_MAX_BUFFERS; ++i)
         pipe_resource_reference(&nvc0->buffers[s][i].buffer, NULL);

      for (i = 0; i < NVC0_MAX_IMAGES; ++i) {
         pipe_resource_reference(&nvc0->images[s][i].resource, NULL);
         if (nvc0->screen->base.class_3d >= GM107_3D_CLASS)
            pipe_sampler_view_reference(&nvc0->images_tic[s][i], NULL);
      }
   }

   for (s = 0; s < 2; ++s) {
      for (i = 0; i < NVC0_MAX_SURFACE_SLOTS; ++i)
         pipe_surface_reference(&nvc0->surfaces[s][i], NULL);
   }

   for (i = 0; i < nvc0->num_tfbbufs; ++i)
      pipe_so_target_reference(&nvc0->tfbbuf[i], NULL);

   for (i = 0; i < nvc0->global_residents.size / sizeof(struct pipe_resource *);
        ++i) {
      struct pipe_resource **res = util_dynarray_element(
         &nvc0->global_residents, struct pipe_resource *, i);
      pipe_resource_reference(res, NULL);
   }
   util_dynarray_fini(&nvc0->global_residents);

   if (nvc0->tcp_empty)
      nvc0->base.pipe.delete_tcs_state(&nvc0->base.pipe, nvc0->tcp_empty);
}

static void
nvc0_destroy(struct pipe_context *pipe)
{
   struct nvc0_context *nvc0 = nvc0_context(pipe);

   if (nvc0->screen->cur_ctx == nvc0) {
      nvc0->screen->cur_ctx = NULL;
      nvc0->save_state = nvc0->state;
      nvc0->save_state.tfb = NULL;
   }

   if (nvc0->base.pipe.stream_uploader)
      u_upload_destroy(nvc0->base.pipe.stream_uploader);

   /* Unset bufctx, we don't want to revalidate any resources after the flush.
    * Other contexts will always set their bufctx again on action calls.
    */
   nouveau_pushbuf_bufctx(nvc0->base.pushbuf, NULL);
   nouveau_pushbuf_kick(nvc0->base.pushbuf, nvc0->base.pushbuf->channel);

   nvc0_context_unreference_resources(nvc0);
   nvc0_blitctx_destroy(nvc0);

   list_for_each_entry_safe(struct nvc0_resident, pos, &nvc0->tex_head, list) {
      list_del(&pos->list);
      free(pos);
   }

   list_for_each_entry_safe(struct nvc0_resident, pos, &nvc0->img_head, list) {
      list_del(&pos->list);
      free(pos);
   }

   if (nvc0->base.fence.current) {
      struct nouveau_fence *current = NULL;

      /* nouveau_fence_wait will create a new current fence, so wait on the
       * _current_ one, and remove both.
       */
      nouveau_fence_ref(nvc0->base.fence.current, &current);
      nouveau_fence_wait(current, NULL);
      nouveau_fence_ref(NULL, &current);
      nouveau_fence_ref(NULL, &nvc0->base.fence.current);
   }

   if (nvc0->base.pushbuf)
      nvc0->base.pushbuf->user_priv = NULL;

   if (nvc0->blitter)
      nvc0_blitter_destroy(nvc0);

   nouveau_bo_ref(NULL, &nvc0->text);
   nouveau_bo_ref(NULL, &nvc0->uniform_bo);
   nouveau_bo_ref(NULL, &nvc0->tls);
   nouveau_bo_ref(NULL, &nvc0->txc);
   nouveau_bo_ref(NULL, &nvc0->fence.bo);
   nouveau_bo_ref(NULL, &nvc0->poly_cache);

   nouveau_heap_destroy(&nvc0->lib_code);
   nouveau_heap_destroy(&nvc0->text_heap);

   FREE(nvc0->default_tsc);
   FREE(nvc0->tic.entries);

   nouveau_object_del(&nvc0->eng3d);
   nouveau_object_del(&nvc0->eng2d);
   nouveau_object_del(&nvc0->m2mf);
   nouveau_object_del(&nvc0->compute);
   nouveau_object_del(&nvc0->nvsw);

   nouveau_context_destroy(&nvc0->base);
}

void
nvc0_default_kick_notify(struct nouveau_pushbuf *push)
{
   struct nvc0_context *context = push->user_priv;

   if (context) {
      nouveau_fence_next(&context->base);
      nouveau_fence_update(&context->base, true);
      context->state.flushed = true;
      NOUVEAU_DRV_STAT(&context->screen->base, pushbuf_count, 1);
   }
}

static int
nvc0_invalidate_resource_storage(struct nouveau_context *ctx,
                                 struct pipe_resource *res,
                                 int ref)
{
   struct nvc0_context *nvc0 = nvc0_context(&ctx->pipe);
   unsigned s, i;

   if (res->bind & PIPE_BIND_RENDER_TARGET) {
      for (i = 0; i < nvc0->framebuffer.nr_cbufs; ++i) {
         if (nvc0->framebuffer.cbufs[i] &&
             nvc0->framebuffer.cbufs[i]->texture == res) {
            nvc0->dirty_3d |= NVC0_NEW_3D_FRAMEBUFFER;
            nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_FB);
            if (!--ref)
               return ref;
         }
      }
   }
   if (res->bind & PIPE_BIND_DEPTH_STENCIL) {
      if (nvc0->framebuffer.zsbuf &&
          nvc0->framebuffer.zsbuf->texture == res) {
         nvc0->dirty_3d |= NVC0_NEW_3D_FRAMEBUFFER;
         nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_FB);
         if (!--ref)
            return ref;
      }
   }

   if (res->target == PIPE_BUFFER) {
      for (i = 0; i < nvc0->num_vtxbufs; ++i) {
         if (nvc0->vtxbuf[i].buffer.resource == res) {
            nvc0->dirty_3d |= NVC0_NEW_3D_ARRAYS;
            nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_VTX);
            if (!--ref)
               return ref;
         }
      }

      for (s = 0; s < 6; ++s) {
         for (i = 0; i < nvc0->num_textures[s]; ++i) {
            if (nvc0->textures[s][i] &&
                nvc0->textures[s][i]->texture == res) {
               nvc0->textures_dirty[s] |= 1 << i;
               if (unlikely(s == 5)) {
                  nvc0->dirty_cp |= NVC0_NEW_CP_TEXTURES;
                  nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_TEX(i));
               } else {
                  nvc0->dirty_3d |= NVC0_NEW_3D_TEXTURES;
                  nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_TEX(s, i));
               }
               if (!--ref)
                  return ref;
            }
         }
      }

      for (s = 0; s < 6; ++s) {
         for (i = 0; i < NVC0_MAX_PIPE_CONSTBUFS; ++i) {
            if (!(nvc0->constbuf_valid[s] & (1 << i)))
               continue;
            if (!nvc0->constbuf[s][i].user &&
                nvc0->constbuf[s][i].u.buf == res) {
               nvc0->constbuf_dirty[s] |= 1 << i;
               if (unlikely(s == 5)) {
                  nvc0->dirty_cp |= NVC0_NEW_CP_CONSTBUF;
                  nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_CB(i));
               } else {
                  nvc0->dirty_3d |= NVC0_NEW_3D_CONSTBUF;
                  nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_CB(s, i));
               }
               if (!--ref)
                  return ref;
            }
         }
      }

      for (s = 0; s < 6; ++s) {
         for (i = 0; i < NVC0_MAX_BUFFERS; ++i) {
            if (nvc0->buffers[s][i].buffer == res) {
               nvc0->buffers_dirty[s] |= 1 << i;
               if (unlikely(s == 5)) {
                  nvc0->dirty_cp |= NVC0_NEW_CP_BUFFERS;
                  nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_BUF);
               } else {
                  nvc0->dirty_3d |= NVC0_NEW_3D_BUFFERS;
                  nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_BUF);
               }
               if (!--ref)
                  return ref;
            }
         }
      }

      for (s = 0; s < 6; ++s) {
         for (i = 0; i < NVC0_MAX_IMAGES; ++i) {
            if (nvc0->images[s][i].resource == res) {
               nvc0->images_dirty[s] |= 1 << i;
               if (unlikely(s == 5)) {
                  nvc0->dirty_cp |= NVC0_NEW_CP_SURFACES;
                  nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_SUF);
               } else {
                  nvc0->dirty_3d |= NVC0_NEW_3D_SURFACES;
                  nouveau_bufctx_reset(nvc0->bufctx_3d, NVC0_BIND_3D_SUF);
               }
            }
            if (!--ref)
               return ref;
         }
      }
   }

   return ref;
}

static void
nvc0_context_fence_emit(struct pipe_context *pcontext, u32 *sequence)
{
   struct nvc0_context *context = nvc0_context(pcontext);
   struct nouveau_pushbuf *push = context->base.pushbuf;

   /* we need to do it after possible flush in MARK_RING */
   *sequence = ++context->base.fence.sequence;

   assert(PUSH_AVAIL(push) + push->rsvd_kick >= 5);
   PUSH_DATA (push, NVC0_FIFO_PKHDR_SQ(NVC0_3D(QUERY_ADDRESS_HIGH), 4));
   PUSH_DATAh(push, context->fence.bo->offset);
   PUSH_DATA (push, context->fence.bo->offset);
   PUSH_DATA (push, *sequence);
   PUSH_DATA (push, NVC0_3D_QUERY_GET_FENCE | NVC0_3D_QUERY_GET_SHORT |
              (0xf << NVC0_3D_QUERY_GET_UNIT__SHIFT));
}

static void
nvc0_magic_3d_init(struct nouveau_pushbuf *push, uint16_t obj_class)
{
   BEGIN_NVC0(push, SUBC_3D(0x10cc), 1);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_3D(0x10e0), 2);
   PUSH_DATA (push, 0xff);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_3D(0x10ec), 2);
   PUSH_DATA (push, 0xff);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_3D(0x074c), 1);
   PUSH_DATA (push, 0x3f);

   BEGIN_NVC0(push, SUBC_3D(0x16a8), 1);
   PUSH_DATA (push, (3 << 16) | 3);
   BEGIN_NVC0(push, SUBC_3D(0x1794), 1);
   PUSH_DATA (push, (2 << 16) | 2);

   if (obj_class < GM107_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x12ac), 1);
      PUSH_DATA (push, 0);
   }
   BEGIN_NVC0(push, SUBC_3D(0x0218), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x10fc), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1290), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x12d8), 2);
   PUSH_DATA (push, 0x10);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1140), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1610), 1);
   PUSH_DATA (push, 0xe);

   BEGIN_NVC0(push, NVC0_3D(VERTEX_ID_GEN_MODE), 1);
   PUSH_DATA (push, NVC0_3D_VERTEX_ID_GEN_MODE_DRAW_ARRAYS_ADD_START);
   BEGIN_NVC0(push, SUBC_3D(0x030c), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, SUBC_3D(0x0300), 1);
   PUSH_DATA (push, 3);

   BEGIN_NVC0(push, SUBC_3D(0x02d0), 1);
   PUSH_DATA (push, 0x3fffff);
   BEGIN_NVC0(push, SUBC_3D(0x0fdc), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, SUBC_3D(0x19c0), 1);
   PUSH_DATA (push, 1);

   if (obj_class < GM107_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x075c), 1);
      PUSH_DATA (push, 3);

      if (obj_class >= NVE4_3D_CLASS) {
         BEGIN_NVC0(push, SUBC_3D(0x07fc), 1);
         PUSH_DATA (push, 1);
      }
   }

   /* TODO: find out what software methods 0x1528, 0x1280 and (on nve4) 0x02dc
    * are supposed to do */
}

static int
nvc0_graph_set_macro(struct nvc0_context *nvc0, uint32_t m, unsigned pos,
                     unsigned size, const uint32_t *data)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;

   size /= 4;

   assert((pos + size) <= 0x800);

   BEGIN_NVC0(push, SUBC_3D(NVC0_GRAPH_MACRO_ID), 2);
   PUSH_DATA (push, (m - 0x3800) / 8);
   PUSH_DATA (push, pos);
   BEGIN_1IC0(push, SUBC_3D(NVC0_GRAPH_MACRO_UPLOAD_POS), size + 1);
   PUSH_DATA (push, pos);
   PUSH_DATAp(push, data, size);

   return pos + size;
}

static u32
nvc0_context_fence_update(struct pipe_context *pcontext)
{
   struct nvc0_context *context = nvc0_context(pcontext);
   return context->fence.map[0];
}

static int
nvc0_context_init_compute(struct nvc0_context *nvc0)
{
   switch (nvc0->screen->base.device->chipset & ~0xf) {
   case 0xc0:
   case 0xd0:
      return nvc0_context_compute_setup(nvc0, nvc0->base.pushbuf);
   case 0xe0:
   case 0xf0:
   case 0x100:
   case 0x110:
   case 0x120:
   case 0x130:
      return nve4_context_compute_setup(nvc0, nvc0->base.pushbuf);
   default:
      return -1;
   }
}

static int
nvc0_context_resize_tls_area(struct nvc0_context *nvc0,
                             uint32_t lpos, uint32_t lneg, uint32_t cstack)
{
   struct nouveau_bo *bo = NULL;
   int ret;
   uint64_t size = (lpos + lneg) * 32 + cstack;

   if (size >= (1 << 20)) {
      NOUVEAU_ERR("requested TLS size too large: 0x%"PRIx64"\n", size);
      return -1;
   }

   size *= (nvc0->screen->base.device->chipset >= 0xe0) ? 64 : 48; /* max warps */
   size  = align(size, 0x8000);
   size *= nvc0->screen->mp_count;

   size = align(size, 1 << 17);

   ret = nouveau_bo_new(nvc0->screen->base.device, NV_VRAM_DOMAIN(&nvc0->screen->base), 1 << 17, size,
                        NULL, &bo);
   if (ret)
      return ret;

   /* Make sure that the pushbuf has acquired a reference to the old tls
    * segment, as it may have commands that will reference it.
    */
   if (nvc0->tls)
      PUSH_REFN(nvc0->base.pushbuf, nvc0->tls,
                NV_VRAM_DOMAIN(&nvc0->screen->base) | NOUVEAU_BO_RDWR);
   nouveau_bo_ref(NULL, &nvc0->tls);
   nvc0->tls = bo;
   return 0;
}

int
nvc0_context_resize_text_area(struct nvc0_context *nvc0, uint64_t size)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nouveau_bo *bo;
   int ret;

   ret = nouveau_bo_new(nvc0->screen->base.device, NV_VRAM_DOMAIN(&nvc0->screen->base),
                        1 << 17, size, NULL, &bo);
   if (ret)
      return ret;

   /* Make sure that the pushbuf has acquired a reference to the old text
    * segment, as it may have commands that will reference it.
    */
   if (nvc0->text)
      PUSH_REFN(push, nvc0->text,
                NV_VRAM_DOMAIN(&nvc0->screen->base) | NOUVEAU_BO_RD);
   nouveau_bo_ref(NULL, &nvc0->text);
   nvc0->text = bo;

   nouveau_heap_destroy(&nvc0->lib_code);
   nouveau_heap_destroy(&nvc0->text_heap);

   /* XXX: getting a page fault at the end of the code buffer every few
    *  launches, don't use the last 256 bytes to work around them - prefetch ?
    */
   nouveau_heap_init(&nvc0->text_heap, 0, size - 0x100);

   /* update the code segment setup */
   BEGIN_NVC0(push, NVC0_3D(CODE_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nvc0->text->offset);
   PUSH_DATA (push, nvc0->text->offset);
   if (nvc0->compute) {
      BEGIN_NVC0(push, NVC0_CP(CODE_ADDRESS_HIGH), 2);
      PUSH_DATAh(push, nvc0->text->offset);
      PUSH_DATA (push, nvc0->text->offset);
   }

   return 0;
}

void
nvc0_context_bind_cb_3d(struct nvc0_context *nvc0, bool *can_serialize,
                        int stage, int index, int size, uint64_t addr)
{
   assert(stage != 5);

   struct nouveau_pushbuf *push = nvc0->base.pushbuf;

   if (nvc0->screen->base.class_3d >= GM107_3D_CLASS) {
      struct nvc0_cb_binding *binding = &nvc0->cb_bindings[stage][index];

      // TODO: Better figure out the conditions in which this is needed
      bool serialize = binding->addr == addr && binding->size != size;
      if (can_serialize)
         serialize = serialize && *can_serialize;
      if (serialize) {
         IMMED_NVC0(push, NVC0_3D(SERIALIZE), 0);
         if (can_serialize)
            *can_serialize = false;
      }

      binding->addr = addr;
      binding->size = size;
   }

   if (size >= 0) {
      BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
      PUSH_DATA (push, size);
      PUSH_DATAh(push, addr);
      PUSH_DATA (push, addr);
   }
   IMMED_NVC0(push, NVC0_3D(CB_BIND(stage)), (index << 4) | (size >= 0));
}

static void
nvc0_context_get_sample_position(struct pipe_context *, unsigned, unsigned,
                                 float *);

#define FAIL_CONTEXT_INIT(str, err)                   \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      goto out_err;                                   \
   } while(0)


struct pipe_context *
nvc0_create(struct pipe_screen *pscreen, void *priv, unsigned ctxflags)
{
   struct nvc0_screen *screen = nvc0_screen(pscreen);
   struct nvc0_context *nvc0;
   struct pipe_context *pipe;
   struct nouveau_pushbuf *push;
   struct nouveau_object *chan;
   struct nouveau_device *dev = screen->base.device;
   int ret, i;
   uint32_t flags;
   uint32_t obj_class;

   nvc0 = CALLOC_STRUCT(nvc0_context);
   if (!nvc0)
      return NULL;
   pipe = &nvc0->base.pipe;

   if (!nvc0_blitctx_create(nvc0))
      goto out_err;

   nvc0->base.fence.emit = nvc0_context_fence_emit;
   nvc0->base.fence.update = nvc0_context_fence_update;
   nvc0->base.client = screen->base.client;

   nvc0->screen = screen;
   nvc0->base.screen = &screen->base;

   pipe->screen = pscreen;
   pipe->priv = priv;
   pipe->stream_uploader = u_upload_create_default(pipe);
   if (!pipe->stream_uploader)
      goto out_err;
   pipe->const_uploader = pipe->stream_uploader;

   pipe->destroy = nvc0_destroy;

   pipe->draw_vbo = nvc0_draw_vbo;
   pipe->clear = nvc0_clear;
   pipe->launch_grid = (nvc0->screen->base.class_3d >= NVE4_3D_CLASS) ?
      nve4_launch_grid : nvc0_launch_grid;

   pipe->flush = nvc0_flush;
   pipe->texture_barrier = nvc0_texture_barrier;
   pipe->memory_barrier = nvc0_memory_barrier;
   pipe->get_sample_position = nvc0_context_get_sample_position;
   pipe->emit_string_marker = nvc0_emit_string_marker;

   nouveau_context_init(&nvc0->base);
   chan = nvc0->base.channel;
   /////////////////// new init ///////////////////////////

   flags = NOUVEAU_BO_GART | NOUVEAU_BO_MAP;
   if (screen->base.drm->version >= 0x01000202)
      flags |= NOUVEAU_BO_COHERENT;

   ret = nouveau_bo_new(screen->base.device, flags, 0, 4096, NULL, &nvc0->fence.bo);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating fence BO: %d\n", ret);
   nouveau_bo_map(nvc0->fence.bo, 0, NULL);
   nvc0->fence.map = nvc0->fence.bo->map;

   nouveau_fence_new(&nvc0->base, &nvc0->base.fence.current);

   push = nvc0->base.pushbuf;
   push->user_priv = nvc0;
   push->rsvd_kick = 5;

   ret = nouveau_object_new(chan, (dev->chipset < 0xe0) ? 0x1f906e : 0x906e,
                            NVIF_CLASS_SW_GF100, NULL, 0, &nvc0->nvsw);
   if (ret)
      FAIL_CONTEXT_INIT("Error creating SW object: %d\n", ret);

   BEGIN_NVC0(push, SUBC_SW(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nvc0->nvsw->handle);

   switch (dev->chipset & ~0xf) {
   case 0x130:
   case 0x120:
   case 0x110:
   case 0x100:
   case 0xf0:
      obj_class = NVF0_P2MF_CLASS;
      break;
   case 0xe0:
      obj_class = NVE4_P2MF_CLASS;
      break;
   default:
      obj_class = NVC0_M2MF_CLASS;
      break;
   }
   ret = nouveau_object_new(chan, 0xbeef323f, obj_class, NULL, 0,
                            &nvc0->m2mf);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating PGRAPH context for M2MF: %d\n", ret);

   BEGIN_NVC0(push, SUBC_M2MF(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nvc0->m2mf->oclass);
   if (nvc0->m2mf->oclass == NVE4_P2MF_CLASS) {
      BEGIN_NVC0(push, SUBC_COPY(NV01_SUBCHAN_OBJECT), 1);
      PUSH_DATA (push, 0xa0b5);
   }

   ret = nouveau_object_new(chan, 0xbeef902d, NVC0_2D_CLASS, NULL, 0,
                            &nvc0->eng2d);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating PGRAPH context for 2D: %d\n", ret);

   BEGIN_NVC0(push, SUBC_2D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nvc0->eng2d->oclass);
   BEGIN_NVC0(push, SUBC_2D(NVC0_2D_SINGLE_GPC), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_2D(OPERATION), 1);
   PUSH_DATA (push, NV50_2D_OPERATION_SRCCOPY);
   BEGIN_NVC0(push, NVC0_2D(CLIP_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_2D(COLOR_KEY_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, SUBC_2D(0x0884), 1);
   PUSH_DATA (push, 0x3f);
   BEGIN_NVC0(push, SUBC_2D(0x0888), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_2D(COND_MODE), 1);
   PUSH_DATA (push, NV50_2D_COND_MODE_ALWAYS);

   BEGIN_NVC0(push, SUBC_2D(NVC0_GRAPH_NOTIFY_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nvc0->fence.bo->offset + 16);
   PUSH_DATA (push, nvc0->fence.bo->offset + 16);

   ret = nouveau_object_new(chan, 0xbeef003d, screen->base.class_3d, NULL, 0,
                            &nvc0->eng3d);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating PGRAPH context for 3D: %d\n", ret);

   BEGIN_NVC0(push, SUBC_3D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nvc0->eng3d->oclass);

   BEGIN_NVC0(push, NVC0_3D(COND_MODE), 1);
   PUSH_DATA (push, NVC0_3D_COND_MODE_ALWAYS);

   if (debug_get_bool_option("NOUVEAU_SHADER_WATCHDOG", true)) {
      /* kill shaders after about 1 second (at 100 MHz) */
      BEGIN_NVC0(push, NVC0_3D(WATCHDOG_TIMER), 1);
      PUSH_DATA (push, 0x17);
   }

   IMMED_NVC0(push, NVC0_3D(ZETA_COMP_ENABLE),
                    screen->base.drm->version >= 0x01000101);
   BEGIN_NVC0(push, NVC0_3D(RT_COMP_ENABLE(0)), 8);
   for (i = 0; i < 8; ++i)
      PUSH_DATA(push, screen->base.drm->version >= 0x01000101);

   BEGIN_NVC0(push, NVC0_3D(RT_CONTROL), 1);
   PUSH_DATA (push, 1);

   BEGIN_NVC0(push, NVC0_3D(CSAA_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_MODE), 1);
   PUSH_DATA (push, NVC0_3D_MULTISAMPLE_MODE_MS1);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_CTRL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(LINE_WIDTH_SEPARATE), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(PRIM_RESTART_WITH_DRAW_ARRAYS), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(BLEND_SEPARATE_ALPHA), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(BLEND_ENABLE_COMMON), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(SHADE_MODEL), 1);
   PUSH_DATA (push, NVC0_3D_SHADE_MODEL_SMOOTH);
   if (nvc0->eng3d->oclass < NVE4_3D_CLASS) {
      IMMED_NVC0(push, NVC0_3D(TEX_MISC), 0);
   } else {
      BEGIN_NVC0(push, NVE4_3D(TEX_CB_INDEX), 1);
      PUSH_DATA (push, 15);
   }
   BEGIN_NVC0(push, NVC0_3D(CALL_LIMIT_LOG), 1);
   PUSH_DATA (push, 8); /* 128 */
   BEGIN_NVC0(push, NVC0_3D(ZCULL_STATCTRS_ENABLE), 1);
   PUSH_DATA (push, 1);
   if (nvc0->eng3d->oclass >= NVC1_3D_CLASS) {
      BEGIN_NVC0(push, NVC0_3D(CACHE_SPLIT), 1);
      PUSH_DATA (push, NVC0_3D_CACHE_SPLIT_48K_SHARED_16K_L1);
   }

   nvc0_magic_3d_init(push, nvc0->eng3d->oclass);

   ret = nvc0_context_resize_text_area(nvc0, 1 << 19);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating TEXT area: %d\n", ret);

   /* 6 user uniform areas, 6 driver areas, and 1 for the runout */
   ret = nouveau_bo_new(screen->base.device, NV_VRAM_DOMAIN(&screen->base), 1 << 12, 13 << 16, NULL,
                        &nvc0->uniform_bo);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating uniform BO: %d\n", ret);

   PUSH_REFN (push, nvc0->uniform_bo, NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_WR);

   /* return { 0.0, 0.0, 0.0, 0.0 } for out-of-bounds vtxbuf access */
   BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
   PUSH_DATA (push, 256);
   PUSH_DATAh(push, nvc0->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   PUSH_DATA (push, nvc0->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   BEGIN_1IC0(push, NVC0_3D(CB_POS), 5);
   PUSH_DATA (push, 0);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   BEGIN_NVC0(push, NVC0_3D(VERTEX_RUNOUT_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nvc0->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   PUSH_DATA (push, nvc0->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);

   ret = nvc0_context_resize_tls_area(nvc0, 128 * 16, 0, 0x200);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating TLS area: %d\n", ret);

   BEGIN_NVC0(push, NVC0_3D(TEMP_ADDRESS_HIGH), 4);
   PUSH_DATAh(push, nvc0->tls->offset);
   PUSH_DATA (push, nvc0->tls->offset);
   PUSH_DATA (push, nvc0->tls->size >> 32);
   PUSH_DATA (push, nvc0->tls->size);
   BEGIN_NVC0(push, NVC0_3D(WARP_TEMP_ALLOC), 1);
   PUSH_DATA (push, 0);
   /* Reduce likelihood of collision with real buffers by placing the hole at
    * the top of the 4G area. This will have to be dealt with for real
    * eventually by blocking off that area from the VM.
    */
   BEGIN_NVC0(push, NVC0_3D(LOCAL_BASE), 1);
   PUSH_DATA (push, 0xff << 24);

   if (nvc0->eng3d->oclass < GM107_3D_CLASS) {
      ret = nouveau_bo_new(screen->base.device, NV_VRAM_DOMAIN(&screen->base), 1 << 17, 1 << 20, NULL,
                           &nvc0->poly_cache);
      if (ret)
         FAIL_CONTEXT_INIT("Error allocating poly cache BO: %d\n", ret);

      BEGIN_NVC0(push, NVC0_3D(VERTEX_QUARANTINE_ADDRESS_HIGH), 3);
      PUSH_DATAh(push, nvc0->poly_cache->offset);
      PUSH_DATA (push, nvc0->poly_cache->offset);
      PUSH_DATA (push, 3);
   }

   ret = nouveau_bo_new(screen->base.device, NV_VRAM_DOMAIN(&screen->base), 1 << 17, 1 << 17, NULL,
                        &nvc0->txc);
   if (ret)
      FAIL_CONTEXT_INIT("Error allocating txc BO: %d\n", ret);

   BEGIN_NVC0(push, NVC0_3D(TIC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nvc0->txc->offset);
   PUSH_DATA (push, nvc0->txc->offset);
   PUSH_DATA (push, NVC0_TIC_MAX_ENTRIES - 1);
   if (nvc0->eng3d->oclass >= GM107_3D_CLASS) {
      nvc0->tic.maxwell = true;
      if (nvc0->eng3d->oclass == GM107_3D_CLASS) {
         nvc0->tic.maxwell =
            debug_get_bool_option("NOUVEAU_MAXWELL_TIC", true);
         IMMED_NVC0(push, SUBC_3D(0x0f10), nvc0->tic.maxwell);
      }
   }

   BEGIN_NVC0(push, NVC0_3D(TSC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nvc0->txc->offset + 65536);
   PUSH_DATA (push, nvc0->txc->offset + 65536);
   PUSH_DATA (push, NVC0_TSC_MAX_ENTRIES - 1);

   BEGIN_NVC0(push, NVC0_3D(SCREEN_Y_CONTROL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(WINDOW_OFFSET_X), 2);
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(ZCULL_REGION), 1); /* deactivate ZCULL */
   PUSH_DATA (push, 0x3f);

   BEGIN_NVC0(push, NVC0_3D(CLIP_RECTS_MODE), 1);
   PUSH_DATA (push, NVC0_3D_CLIP_RECTS_MODE_INSIDE_ANY);
   BEGIN_NVC0(push, NVC0_3D(CLIP_RECT_HORIZ(0)), 8 * 2);
   for (i = 0; i < 8 * 2; ++i)
      PUSH_DATA(push, 0);
   BEGIN_NVC0(push, NVC0_3D(CLIP_RECTS_EN), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(CLIPID_ENABLE), 1);
   PUSH_DATA (push, 0);

   /* neither scissors, viewport nor stencil mask should affect clears */
   BEGIN_NVC0(push, NVC0_3D(CLEAR_FLAGS), 1);
   PUSH_DATA (push, 0);

   BEGIN_NVC0(push, NVC0_3D(VIEWPORT_TRANSFORM_EN), 1);
   PUSH_DATA (push, 1);
   for (i = 0; i < NVC0_MAX_VIEWPORTS; i++) {
      BEGIN_NVC0(push, NVC0_3D(DEPTH_RANGE_NEAR(i)), 2);
      PUSH_DATAf(push, 0.0f);
      PUSH_DATAf(push, 1.0f);
   }
   BEGIN_NVC0(push, NVC0_3D(VIEW_VOLUME_CLIP_CTRL), 1);
   PUSH_DATA (push, NVC0_3D_VIEW_VOLUME_CLIP_CTRL_UNK1_UNK1);

   /* We use scissors instead of exact view volume clipping,
    * so they're always enabled.
    */
   for (i = 0; i < NVC0_MAX_VIEWPORTS; i++) {
      BEGIN_NVC0(push, NVC0_3D(SCISSOR_ENABLE(i)), 3);
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 8192 << 16);
      PUSH_DATA (push, 8192 << 16);
   }

#define MK_MACRO(m, n) i = nvc0_graph_set_macro(nvc0, m, i, sizeof(n), n);

   i = 0;
   MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_PER_INSTANCE, mme9097_per_instance_bf);
   MK_MACRO(NVC0_3D_MACRO_BLEND_ENABLES, mme9097_blend_enables);
   MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_SELECT, mme9097_vertex_array_select);
   MK_MACRO(NVC0_3D_MACRO_TEP_SELECT, mme9097_tep_select);
   MK_MACRO(NVC0_3D_MACRO_GP_SELECT, mme9097_gp_select);
   MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_FRONT, mme9097_poly_mode_front);
   MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_BACK, mme9097_poly_mode_back);
   MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT, mme9097_draw_arrays_indirect);
   MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT, mme9097_draw_elts_indirect);
   MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT_COUNT, mme9097_draw_arrays_indirect_count);
   MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT_COUNT, mme9097_draw_elts_indirect_count);
   MK_MACRO(NVC0_3D_MACRO_QUERY_BUFFER_WRITE, mme9097_query_buffer_write);
   MK_MACRO(NVC0_3D_MACRO_CONSERVATIVE_RASTER_STATE, mme9097_conservative_raster_state);
   MK_MACRO(NVC0_CP_MACRO_LAUNCH_GRID_INDIRECT, mme90c0_launch_grid_indirect);

   BEGIN_NVC0(push, NVC0_3D(RASTERIZE_ENABLE), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(RT_SEPARATE_FRAG_DATA), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(MACRO_GP_SELECT), 1);
   PUSH_DATA (push, 0x40);
   BEGIN_NVC0(push, NVC0_3D(LAYER), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MACRO_TEP_SELECT), 1);
   PUSH_DATA (push, 0x30);
   BEGIN_NVC0(push, NVC0_3D(PATCH_VERTICES), 1);
   PUSH_DATA (push, 3);
   BEGIN_NVC0(push, NVC0_3D(SP_SELECT(2)), 1);
   PUSH_DATA (push, 0x20);
   BEGIN_NVC0(push, NVC0_3D(SP_SELECT(0)), 1);
   PUSH_DATA (push, 0x00);
   nvc0->save_state.patch_vertices = 3;

   BEGIN_NVC0(push, NVC0_3D(POINT_COORD_REPLACE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(POINT_RASTER_RULES), 1);
   PUSH_DATA (push, NVC0_3D_POINT_RASTER_RULES_OGL);

   IMMED_NVC0(push, NVC0_3D(EDGEFLAG), 1);

   if (nvc0_context_init_compute(nvc0))
      goto out_err;

   /* XXX: Compute and 3D are somehow aliased on Fermi. */
   for (i = 0; i < 5; ++i) {
      unsigned j = 0;
      for (j = 0; j < 16; j++)
         nvc0->cb_bindings[i][j].size = -1;

      /* TIC and TSC entries for each unit (nve4+ only) */
      /* auxiliary constants (6 user clip planes, base instance id) */
      nvc0_context_bind_cb_3d(nvc0, NULL, i, 15, NVC0_CB_AUX_SIZE,
                              nvc0->uniform_bo->offset + NVC0_CB_AUX_INFO(i));
      if (nvc0->eng3d->oclass >= NVE4_3D_CLASS) {
         unsigned j;
         BEGIN_1IC0(push, NVC0_3D(CB_POS), 9);
         PUSH_DATA (push, NVC0_CB_AUX_UNK_INFO);
         for (j = 0; j < 8; ++j)
            PUSH_DATA(push, j);
      } else {
         BEGIN_NVC0(push, NVC0_3D(TEX_LIMITS(i)), 1);
         PUSH_DATA (push, 0x54);
      }

      /* MS sample coordinate offsets: these do not work with _ALT modes ! */
      BEGIN_1IC0(push, NVC0_3D(CB_POS), 1 + 2 * 8);
      PUSH_DATA (push, NVC0_CB_AUX_MS_INFO);
      PUSH_DATA (push, 0); /* 0 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 1); /* 1 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 0); /* 2 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 1); /* 3 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 2); /* 4 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 3); /* 5 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 2); /* 6 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 3); /* 7 */
      PUSH_DATA (push, 1);
   }
   BEGIN_NVC0(push, NVC0_3D(LINKED_TSC), 1);
   PUSH_DATA (push, 0);

   PUSH_KICK (push);

   nvc0->tic.entries = CALLOC(
         NVC0_TIC_MAX_ENTRIES + NVC0_TSC_MAX_ENTRIES + NVE4_IMG_MAX_HANDLES,
         sizeof(void *));
   nvc0->tsc.entries = nvc0->tic.entries + NVC0_TIC_MAX_ENTRIES;
   nvc0->img.entries = (void *)(nvc0->tsc.entries + NVC0_TSC_MAX_ENTRIES);

   if (!nvc0_blitter_create(nvc0))
      goto out_err;

   nvc0->default_tsc = CALLOC_STRUCT(nv50_tsc_entry);
   nvc0->default_tsc->tsc[0] = G80_TSC_0_SRGB_CONVERSION;

   /////////////////// old init ///////////////////////////
   ret = nouveau_bufctx_new(screen->base.client, 2, &nvc0->bufctx);
   if (!ret)
      ret = nouveau_bufctx_new(screen->base.client, NVC0_BIND_3D_COUNT,
                               &nvc0->bufctx_3d);
   if (!ret)
      ret = nouveau_bufctx_new(screen->base.client, NVC0_BIND_CP_COUNT,
                               &nvc0->bufctx_cp);
   if (ret)
      goto out_err;


   nvc0_init_query_functions(nvc0);
   nvc0_init_surface_functions(nvc0);
   nvc0_init_state_functions(nvc0);
   nvc0_init_transfer_functions(nvc0);
   nvc0_init_resource_functions(pipe);
   if (nvc0->screen->base.class_3d >= NVE4_3D_CLASS)
      nvc0_init_bindless_functions(pipe);

   list_inithead(&nvc0->tex_head);
   list_inithead(&nvc0->img_head);

   nvc0->base.invalidate_resource_storage = nvc0_invalidate_resource_storage;

   pipe->create_video_codec = nvc0_create_decoder;
   pipe->create_video_buffer = nvc0_video_buffer_create;

   /* shader builtin library is per-screen, but we need a context for m2mf */
   nvc0_program_library_upload(nvc0);
   nvc0_program_init_tcp_empty(nvc0);
   if (!nvc0->tcp_empty)
      goto out_err;
   /* set the empty tctl prog on next draw in case one is never set */
   nvc0->dirty_3d |= NVC0_NEW_3D_TCTLPROG;

   /* Do not bind the COMPUTE driver constbuf at screen initialization because
    * CBs are aliased between 3D and COMPUTE, but make sure it will be bound if
    * a grid is launched later. */
   nvc0->dirty_cp |= NVC0_NEW_CP_DRIVERCONST;

   /* now that there are no more opportunities for errors, set the current
    * context if there isn't already one.
    */
   if (!screen->cur_ctx) {
      nvc0->state = nvc0->save_state;
      screen->cur_ctx = nvc0;
      nouveau_pushbuf_bufctx(nvc0->base.pushbuf, nvc0->bufctx);
   }
   nvc0->base.pushbuf->kick_notify = nvc0_default_kick_notify;

   /* add permanently resident buffers to bufctxts */

   flags = NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_RD;

   BCTX_REFN_bo(nvc0->bufctx_3d, 3D_TEXT, flags, nvc0->text);
   BCTX_REFN_bo(nvc0->bufctx_3d, 3D_SCREEN, flags, nvc0->uniform_bo);
   BCTX_REFN_bo(nvc0->bufctx_3d, 3D_SCREEN, flags, nvc0->txc);
   if (nvc0->compute) {
      BCTX_REFN_bo(nvc0->bufctx_cp, CP_TEXT, flags, nvc0->text);
      BCTX_REFN_bo(nvc0->bufctx_cp, CP_SCREEN, flags, nvc0->uniform_bo);
      BCTX_REFN_bo(nvc0->bufctx_cp, CP_SCREEN, flags, nvc0->txc);
   }

   flags = NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_RDWR;

   if (nvc0->poly_cache)
      BCTX_REFN_bo(nvc0->bufctx_3d, 3D_SCREEN, flags, nvc0->poly_cache);
   if (nvc0->compute)
      BCTX_REFN_bo(nvc0->bufctx_cp, CP_SCREEN, flags, nvc0->tls);

   flags = NOUVEAU_BO_GART | NOUVEAU_BO_WR;

   BCTX_REFN_bo(nvc0->bufctx_3d, 3D_SCREEN, flags, nvc0->fence.bo);
   BCTX_REFN_bo(nvc0->bufctx, FENCE, flags, nvc0->fence.bo);
   if (nvc0->compute)
      BCTX_REFN_bo(nvc0->bufctx_cp, CP_SCREEN, flags, nvc0->fence.bo);

   nvc0->base.scratch.bo_size = 2 << 20;

   memset(nvc0->tex_handles, ~0, sizeof(nvc0->tex_handles));

   util_dynarray_init(&nvc0->global_residents, NULL);

   return pipe;

out_err:
   if (nvc0) {
      if (pipe->stream_uploader)
         u_upload_destroy(pipe->stream_uploader);
      if (nvc0->bufctx_3d)
         nouveau_bufctx_del(&nvc0->bufctx_3d);
      if (nvc0->bufctx_cp)
         nouveau_bufctx_del(&nvc0->bufctx_cp);
      if (nvc0->bufctx)
         nouveau_bufctx_del(&nvc0->bufctx);
      FREE(nvc0->blit);
      FREE(nvc0);
   }
   return NULL;
}

void
nvc0_bufctx_fence(struct nvc0_context *nvc0, struct nouveau_bufctx *bufctx,
                  bool on_flush)
{
   struct nouveau_list *list = on_flush ? &bufctx->current : &bufctx->pending;
   struct nouveau_list *it;
   NOUVEAU_DRV_STAT_IFD(unsigned count = 0);

   for (it = list->next; it != list; it = it->next) {
      struct nouveau_bufref *ref = (struct nouveau_bufref *)it;
      struct nv04_resource *res = ref->priv;
      if (res)
         nvc0_resource_validate(res, &nvc0->base, (unsigned)ref->priv_data);
      NOUVEAU_DRV_STAT_IFD(count++);
   }
   NOUVEAU_DRV_STAT(&nvc0->screen->base, resource_validate_count, count);
}

const void *
nvc0_get_sample_locations(unsigned sample_count)
{
   static const uint8_t ms1[1][2] = { { 0x8, 0x8 } };
   static const uint8_t ms2[2][2] = {
      { 0x4, 0x4 }, { 0xc, 0xc } }; /* surface coords (0,0), (1,0) */
   static const uint8_t ms4[4][2] = {
      { 0x6, 0x2 }, { 0xe, 0x6 },   /* (0,0), (1,0) */
      { 0x2, 0xa }, { 0xa, 0xe } }; /* (0,1), (1,1) */
   static const uint8_t ms8[8][2] = {
      { 0x1, 0x7 }, { 0x5, 0x3 },   /* (0,0), (1,0) */
      { 0x3, 0xd }, { 0x7, 0xb },   /* (0,1), (1,1) */
      { 0x9, 0x5 }, { 0xf, 0x1 },   /* (2,0), (3,0) */
      { 0xb, 0xf }, { 0xd, 0x9 } }; /* (2,1), (3,1) */
#if 0
   /* NOTE: there are alternative modes for MS2 and MS8, currently not used */
   static const uint8_t ms8_alt[8][2] = {
      { 0x9, 0x5 }, { 0x7, 0xb },   /* (2,0), (1,1) */
      { 0xd, 0x9 }, { 0x5, 0x3 },   /* (3,1), (1,0) */
      { 0x3, 0xd }, { 0x1, 0x7 },   /* (0,1), (0,0) */
      { 0xb, 0xf }, { 0xf, 0x1 } }; /* (2,1), (3,0) */
#endif

   const uint8_t (*ptr)[2];

   switch (sample_count) {
   case 0:
   case 1: ptr = ms1; break;
   case 2: ptr = ms2; break;
   case 4: ptr = ms4; break;
   case 8: ptr = ms8; break;
   default:
      assert(0);
      return NULL; /* bad sample count -> undefined locations */
   }
   return ptr;
}

static void
nvc0_context_get_sample_position(struct pipe_context *pipe,
                                 unsigned sample_count, unsigned sample_index,
                                 float *xy)
{
   const uint8_t (*ptr)[2];

   ptr = nvc0_get_sample_locations(sample_count);
   if (!ptr)
      return;

   xy[0] = ptr[sample_index][0] * 0.0625f;
   xy[1] = ptr[sample_index][1] * 0.0625f;
}

int
nvc0_context_tic_alloc(struct nvc0_context *nvc0, void *entry)
{
   int i = nvc0->tic.next;

   while (nvc0->tic.lock[i / 32] & (1 << (i % 32)))
      i = (i + 1) & (NVC0_TIC_MAX_ENTRIES - 1);

   nvc0->tic.next = (i + 1) & (NVC0_TIC_MAX_ENTRIES - 1);

   if (nvc0->tic.entries[i])
      nv50_tic_entry(nvc0->tic.entries[i])->id = -1;

   nvc0->tic.entries[i] = entry;
   return i;
}

int
nvc0_context_tsc_alloc(struct nvc0_context *nvc0, void *entry)
{
   int i = nvc0->tsc.next;

   while (nvc0->tsc.lock[i / 32] & (1 << (i % 32)))
      i = (i + 1) & (NVC0_TSC_MAX_ENTRIES - 1);

   nvc0->tsc.next = (i + 1) & (NVC0_TSC_MAX_ENTRIES - 1);

   if (nvc0->tsc.entries[i])
      nv50_tsc_entry(nvc0->tsc.entries[i])->id = -1;

   nvc0->tsc.entries[i] = entry;
   return i;
}
