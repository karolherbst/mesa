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

#include "nv50/nv50_context.h"
#include "nv50/nv50_screen.h"
#include "nv50/nv50_resource.h"

static void
nv50_flush(struct pipe_context *pipe,
           struct pipe_fence_handle **fence,
           unsigned flags)
{
   struct nouveau_context *context = nouveau_context(pipe);

   if (fence)
      nouveau_fence_ref(context->fence.current, (struct nouveau_fence **)fence);

   PUSH_KICK(context->pushbuf);

   nouveau_context_update_frame_stats(nouveau_context(pipe));
}

static void
nv50_texture_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct nouveau_pushbuf *push = nv50_context(pipe)->base.pushbuf;

   BEGIN_NV04(push, SUBC_3D(NV50_GRAPH_SERIALIZE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(TEX_CACHE_CTL), 1);
   PUSH_DATA (push, 0x20);
}

static void
nv50_memory_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct nv50_context *nv50 = nv50_context(pipe);
   int i, s;

   if (flags & PIPE_BARRIER_MAPPED_BUFFER) {
      for (i = 0; i < nv50->num_vtxbufs; ++i) {
         if (!nv50->vtxbuf[i].buffer.resource && !nv50->vtxbuf[i].is_user_buffer)
            continue;
         if (nv50->vtxbuf[i].buffer.resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
            nv50->base.vbo_dirty = true;
      }

      for (s = 0; s < 3 && !nv50->cb_dirty; ++s) {
         uint32_t valid = nv50->constbuf_valid[s];

         while (valid && !nv50->cb_dirty) {
            const unsigned i = ffs(valid) - 1;
            struct pipe_resource *res;

            valid &= ~(1 << i);
            if (nv50->constbuf[s][i].user)
               continue;

            res = nv50->constbuf[s][i].u.buf;
            if (!res)
               continue;

            if (res->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
               nv50->cb_dirty = true;
         }
      }
   }
}

static void
nv50_emit_string_marker(struct pipe_context *pipe, const char *str, int len)
{
   struct nouveau_pushbuf *push = nv50_context(pipe)->base.pushbuf;
   int string_words = len / 4;
   int data_words;

   if (len <= 0)
      return;
   string_words = MIN2(string_words, NV04_PFIFO_MAX_PACKET_LEN);
   if (string_words == NV04_PFIFO_MAX_PACKET_LEN)
      data_words = string_words;
   else
      data_words = string_words + !!(len & 3);
   BEGIN_NI04(push, SUBC_3D(NV04_GRAPH_NOP), data_words);
   if (string_words)
      PUSH_DATAp(push, str, string_words);
   if (string_words != data_words) {
      int data = 0;
      memcpy(&data, &str[string_words * 4], len & 3);
      PUSH_DATA (push, data);
   }
}

void
nv50_default_kick_notify(struct nouveau_pushbuf *push)
{
   struct nv50_context *context = push->user_priv;

   if (context) {
      nouveau_fence_next(&context->base);
      nouveau_fence_update(&context->base, true);
      context->state.flushed = true;
   }
}

static void
nv50_context_unreference_resources(struct nv50_context *nv50)
{
   unsigned s, i;

   nouveau_bufctx_del(&nv50->bufctx_3d);
   nouveau_bufctx_del(&nv50->bufctx);
   nouveau_bufctx_del(&nv50->bufctx_cp);

   util_unreference_framebuffer_state(&nv50->framebuffer);

   assert(nv50->num_vtxbufs <= PIPE_MAX_ATTRIBS);
   for (i = 0; i < nv50->num_vtxbufs; ++i)
      pipe_vertex_buffer_unreference(&nv50->vtxbuf[i]);

   for (s = 0; s < 3; ++s) {
      assert(nv50->num_textures[s] <= PIPE_MAX_SAMPLERS);
      for (i = 0; i < nv50->num_textures[s]; ++i)
         pipe_sampler_view_reference(&nv50->textures[s][i], NULL);

      for (i = 0; i < NV50_MAX_PIPE_CONSTBUFS; ++i)
         if (!nv50->constbuf[s][i].user)
            pipe_resource_reference(&nv50->constbuf[s][i].u.buf, NULL);
   }

   for (i = 0; i < nv50->global_residents.size / sizeof(struct pipe_resource *);
        ++i) {
      struct pipe_resource **res = util_dynarray_element(
         &nv50->global_residents, struct pipe_resource *, i);
      pipe_resource_reference(res, NULL);
   }
   util_dynarray_fini(&nv50->global_residents);
}

static void
nv50_destroy(struct pipe_context *pipe)
{
   struct nv50_context *nv50 = nv50_context(pipe);

   if (nv50->screen->cur_ctx == nv50) {
      nv50->screen->cur_ctx = NULL;
      /* Save off the state in case another context gets created */
      nv50->screen->save_state = nv50->state;
   }

   if (nv50->base.pipe.stream_uploader)
      u_upload_destroy(nv50->base.pipe.stream_uploader);

   nouveau_pushbuf_bufctx(nv50->base.pushbuf, NULL);
   nouveau_pushbuf_kick(nv50->base.pushbuf, nv50->base.pushbuf->channel);

   nv50_context_unreference_resources(nv50);

   FREE(nv50->blit);

   if (nv50->base.fence.current) {
      struct nouveau_fence *current = NULL;

      /* nouveau_fence_wait will create a new current fence, so wait on the
       * _current_ one, and remove both.
       */
      nouveau_fence_ref(nv50->base.fence.current, &current);
      nouveau_fence_wait(current, NULL);
      nouveau_fence_ref(NULL, &current);
      nouveau_fence_ref(NULL, &nv50->base.fence.current);
   }
   if (nv50->base.pushbuf)
      nv50->base.pushbuf->user_priv = NULL;

   nouveau_bo_ref(NULL, &nv50->code);
   nouveau_bo_ref(NULL, &nv50->tls_bo);
   nouveau_bo_ref(NULL, &nv50->stack_bo);
   nouveau_bo_ref(NULL, &nv50->txc);
   nouveau_bo_ref(NULL, &nv50->uniforms);
   nouveau_bo_ref(NULL, &nv50->fence.bo);

   nouveau_heap_destroy(&nv50->vp_code_heap);
   nouveau_heap_destroy(&nv50->gp_code_heap);
   nouveau_heap_destroy(&nv50->fp_code_heap);

   nouveau_object_del(&nv50->tesla);
   nouveau_object_del(&nv50->eng2d);
   nouveau_object_del(&nv50->m2mf);
   nouveau_object_del(&nv50->compute);
   nouveau_object_del(&nv50->sync);

   nouveau_context_destroy(&nv50->base);
}

static int
nv50_invalidate_resource_storage(struct nouveau_context *ctx,
                                 struct pipe_resource *res,
                                 int ref)
{
   struct nv50_context *nv50 = nv50_context(&ctx->pipe);
   unsigned bind = res->bind ? res->bind : PIPE_BIND_VERTEX_BUFFER;
   unsigned s, i;

   if (bind & PIPE_BIND_RENDER_TARGET) {
      assert(nv50->framebuffer.nr_cbufs <= PIPE_MAX_COLOR_BUFS);
      for (i = 0; i < nv50->framebuffer.nr_cbufs; ++i) {
         if (nv50->framebuffer.cbufs[i] &&
             nv50->framebuffer.cbufs[i]->texture == res) {
            nv50->dirty_3d |= NV50_NEW_3D_FRAMEBUFFER;
            nouveau_bufctx_reset(nv50->bufctx_3d, NV50_BIND_3D_FB);
            if (!--ref)
               return ref;
         }
      }
   }
   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (nv50->framebuffer.zsbuf &&
          nv50->framebuffer.zsbuf->texture == res) {
         nv50->dirty_3d |= NV50_NEW_3D_FRAMEBUFFER;
         nouveau_bufctx_reset(nv50->bufctx_3d, NV50_BIND_3D_FB);
         if (!--ref)
            return ref;
      }
   }

   if (bind & (PIPE_BIND_VERTEX_BUFFER |
               PIPE_BIND_INDEX_BUFFER |
               PIPE_BIND_CONSTANT_BUFFER |
               PIPE_BIND_STREAM_OUTPUT |
               PIPE_BIND_SAMPLER_VIEW)) {

      assert(nv50->num_vtxbufs <= PIPE_MAX_ATTRIBS);
      for (i = 0; i < nv50->num_vtxbufs; ++i) {
         if (nv50->vtxbuf[i].buffer.resource == res) {
            nv50->dirty_3d |= NV50_NEW_3D_ARRAYS;
            nouveau_bufctx_reset(nv50->bufctx_3d, NV50_BIND_3D_VERTEX);
            if (!--ref)
               return ref;
         }
      }

      for (s = 0; s < 3; ++s) {
      assert(nv50->num_textures[s] <= PIPE_MAX_SAMPLERS);
      for (i = 0; i < nv50->num_textures[s]; ++i) {
         if (nv50->textures[s][i] &&
             nv50->textures[s][i]->texture == res) {
            nv50->dirty_3d |= NV50_NEW_3D_TEXTURES;
            nouveau_bufctx_reset(nv50->bufctx_3d, NV50_BIND_3D_TEXTURES);
            if (!--ref)
               return ref;
         }
      }
      }

      for (s = 0; s < 3; ++s) {
      for (i = 0; i < NV50_MAX_PIPE_CONSTBUFS; ++i) {
         if (!(nv50->constbuf_valid[s] & (1 << i)))
            continue;
         if (!nv50->constbuf[s][i].user &&
             nv50->constbuf[s][i].u.buf == res) {
            nv50->dirty_3d |= NV50_NEW_3D_CONSTBUF;
            nv50->constbuf_dirty[s] |= 1 << i;
            nouveau_bufctx_reset(nv50->bufctx_3d, NV50_BIND_3D_CB(s, i));
            if (!--ref)
               return ref;
         }
      }
      }
   }

   return ref;
}

static void
nv50_context_get_sample_position(struct pipe_context *, unsigned, unsigned,
                                 float *);

static void
nv50_context_fence_emit(struct pipe_context *pcontext, u32 *sequence)
{
   struct nv50_context *nv50 = nv50_context(pcontext);
   struct nouveau_pushbuf *push = nv50->base.pushbuf;

   /* we need to do it after possible flush in MARK_RING */
   *sequence = ++nv50->base.fence.sequence;

   assert(PUSH_AVAIL(push) + push->rsvd_kick >= 5);
   PUSH_DATA (push, NV50_FIFO_PKHDR(NV50_3D(QUERY_ADDRESS_HIGH), 4));
   PUSH_DATAh(push, nv50->fence.bo->offset);
   PUSH_DATA (push, nv50->fence.bo->offset);
   PUSH_DATA (push, *sequence);
   PUSH_DATA (push, NV50_3D_QUERY_GET_MODE_WRITE_UNK0 |
                    NV50_3D_QUERY_GET_UNK4 |
                    NV50_3D_QUERY_GET_UNIT_CROP |
                    NV50_3D_QUERY_GET_TYPE_QUERY |
                    NV50_3D_QUERY_GET_QUERY_SELECT_ZERO |
                    NV50_3D_QUERY_GET_SHORT);
}

static u32
nv50_context_fence_update(struct pipe_context *pcontext)
{
   return nv50_context(pcontext)->fence.map[0];
}

static void
nv50_context_init_hwctx(struct nv50_context *nv50)
{
   struct nv50_screen *screen = nv50->screen;
   struct nouveau_pushbuf *push = nv50->base.pushbuf;
   struct nv04_fifo *fifo;
   unsigned i;

   fifo = (struct nv04_fifo *)nv50->base.channel->data;

   BEGIN_NV04(push, SUBC_M2MF(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nv50->m2mf->handle);
   BEGIN_NV04(push, SUBC_M2MF(NV03_M2MF_DMA_NOTIFY), 3);
   PUSH_DATA (push, nv50->sync->handle);
   PUSH_DATA (push, fifo->vram);
   PUSH_DATA (push, fifo->vram);

   BEGIN_NV04(push, SUBC_2D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nv50->eng2d->handle);
   BEGIN_NV04(push, NV50_2D(DMA_NOTIFY), 4);
   PUSH_DATA (push, nv50->sync->handle);
   PUSH_DATA (push, fifo->vram);
   PUSH_DATA (push, fifo->vram);
   PUSH_DATA (push, fifo->vram);
   BEGIN_NV04(push, NV50_2D(OPERATION), 1);
   PUSH_DATA (push, NV50_2D_OPERATION_SRCCOPY);
   BEGIN_NV04(push, NV50_2D(CLIP_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_2D(COLOR_KEY_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, SUBC_2D(0x0888), 1);
   PUSH_DATA (push, 1);
   BEGIN_NV04(push, NV50_2D(COND_MODE), 1);
   PUSH_DATA (push, NV50_2D_COND_MODE_ALWAYS);

   BEGIN_NV04(push, SUBC_3D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, nv50->tesla->handle);

   BEGIN_NV04(push, NV50_3D(COND_MODE), 1);
   PUSH_DATA (push, NV50_3D_COND_MODE_ALWAYS);

   BEGIN_NV04(push, NV50_3D(DMA_NOTIFY), 1);
   PUSH_DATA (push, nv50->sync->handle);
   BEGIN_NV04(push, NV50_3D(DMA_ZETA), 11);
   for (i = 0; i < 11; ++i)
      PUSH_DATA(push, fifo->vram);
   BEGIN_NV04(push, NV50_3D(DMA_COLOR(0)), NV50_3D_DMA_COLOR__LEN);
   for (i = 0; i < NV50_3D_DMA_COLOR__LEN; ++i)
      PUSH_DATA(push, fifo->vram);

   BEGIN_NV04(push, NV50_3D(REG_MODE), 1);
   PUSH_DATA (push, NV50_3D_REG_MODE_STRIPED);
   BEGIN_NV04(push, NV50_3D(UNK1400_LANES), 1);
   PUSH_DATA (push, 0xf);

   if (debug_get_bool_option("NOUVEAU_SHADER_WATCHDOG", true)) {
      BEGIN_NV04(push, NV50_3D(WATCHDOG_TIMER), 1);
      PUSH_DATA (push, 0x18);
   }

   BEGIN_NV04(push, NV50_3D(ZETA_COMP_ENABLE), 1);
   PUSH_DATA(push, screen->base.drm->version >= 0x01000101);

   BEGIN_NV04(push, NV50_3D(RT_COMP_ENABLE(0)), 8);
   for (i = 0; i < 8; ++i)
      PUSH_DATA(push, screen->base.drm->version >= 0x01000101);

   BEGIN_NV04(push, NV50_3D(RT_CONTROL), 1);
   PUSH_DATA (push, 1);

   BEGIN_NV04(push, NV50_3D(CSAA_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(MULTISAMPLE_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(MULTISAMPLE_MODE), 1);
   PUSH_DATA (push, NV50_3D_MULTISAMPLE_MODE_MS1);
   BEGIN_NV04(push, NV50_3D(MULTISAMPLE_CTRL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(PRIM_RESTART_WITH_DRAW_ARRAYS), 1);
   PUSH_DATA (push, 1);
   BEGIN_NV04(push, NV50_3D(BLEND_SEPARATE_ALPHA), 1);
   PUSH_DATA (push, 1);

   if (nv50->tesla->oclass >= NVA0_3D_CLASS) {
      BEGIN_NV04(push, SUBC_3D(NVA0_3D_TEX_MISC), 1);
      PUSH_DATA (push, 0);
   }

   BEGIN_NV04(push, NV50_3D(SCREEN_Y_CONTROL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(WINDOW_OFFSET_X), 2);
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(ZCULL_REGION), 1);
   PUSH_DATA (push, 0x3f);

   BEGIN_NV04(push, NV50_3D(VP_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nv50->code->offset + (0 << NV50_CODE_BO_SIZE_LOG2));
   PUSH_DATA (push, nv50->code->offset + (0 << NV50_CODE_BO_SIZE_LOG2));

   BEGIN_NV04(push, NV50_3D(FP_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nv50->code->offset + (1 << NV50_CODE_BO_SIZE_LOG2));
   PUSH_DATA (push, nv50->code->offset + (1 << NV50_CODE_BO_SIZE_LOG2));

   BEGIN_NV04(push, NV50_3D(GP_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nv50->code->offset + (2 << NV50_CODE_BO_SIZE_LOG2));
   PUSH_DATA (push, nv50->code->offset + (2 << NV50_CODE_BO_SIZE_LOG2));

   BEGIN_NV04(push, NV50_3D(LOCAL_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->tls_bo->offset);
   PUSH_DATA (push, nv50->tls_bo->offset);
   PUSH_DATA (push, util_logbase2(nv50->cur_tls_space / 8));

   BEGIN_NV04(push, NV50_3D(STACK_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->stack_bo->offset);
   PUSH_DATA (push, nv50->stack_bo->offset);
   PUSH_DATA (push, 4);

   BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->uniforms->offset + (0 << 16));
   PUSH_DATA (push, nv50->uniforms->offset + (0 << 16));
   PUSH_DATA (push, (NV50_CB_PVP << 16) | 0x0000);

   BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->uniforms->offset + (1 << 16));
   PUSH_DATA (push, nv50->uniforms->offset + (1 << 16));
   PUSH_DATA (push, (NV50_CB_PGP << 16) | 0x0000);

   BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->uniforms->offset + (2 << 16));
   PUSH_DATA (push, nv50->uniforms->offset + (2 << 16));
   PUSH_DATA (push, (NV50_CB_PFP << 16) | 0x0000);

   BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->uniforms->offset + (3 << 16));
   PUSH_DATA (push, nv50->uniforms->offset + (3 << 16));
   PUSH_DATA (push, (NV50_CB_AUX << 16) | (NV50_CB_AUX_SIZE & 0xffff));

   BEGIN_NI04(push, NV50_3D(SET_PROGRAM_CB), 3);
   PUSH_DATA (push, (NV50_CB_AUX << 12) | 0xf01);
   PUSH_DATA (push, (NV50_CB_AUX << 12) | 0xf21);
   PUSH_DATA (push, (NV50_CB_AUX << 12) | 0xf31);

   /* return { 0.0, 0.0, 0.0, 0.0 } on out-of-bounds vtxbuf access */
   BEGIN_NV04(push, NV50_3D(CB_ADDR), 1);
   PUSH_DATA (push, (NV50_CB_AUX_RUNOUT_OFFSET << (8 - 2)) | NV50_CB_AUX);
   BEGIN_NI04(push, NV50_3D(CB_DATA(0)), 4);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   BEGIN_NV04(push, NV50_3D(VERTEX_RUNOUT_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, nv50->uniforms->offset + (3 << 16) + NV50_CB_AUX_RUNOUT_OFFSET);
   PUSH_DATA (push, nv50->uniforms->offset + (3 << 16) + NV50_CB_AUX_RUNOUT_OFFSET);

   nv50_upload_ms_info(push);

   /* max TIC (bits 4:8) & TSC bindings, per program type */
   for (i = 0; i < 3; ++i) {
      BEGIN_NV04(push, NV50_3D(TEX_LIMITS(i)), 1);
      PUSH_DATA (push, 0x54);
   }

   BEGIN_NV04(push, NV50_3D(TIC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->txc->offset);
   PUSH_DATA (push, nv50->txc->offset);
   PUSH_DATA (push, NV50_TIC_MAX_ENTRIES - 1);

   BEGIN_NV04(push, NV50_3D(TSC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->txc->offset + 65536);
   PUSH_DATA (push, nv50->txc->offset + 65536);
   PUSH_DATA (push, NV50_TSC_MAX_ENTRIES - 1);

   BEGIN_NV04(push, NV50_3D(LINKED_TSC), 1);
   PUSH_DATA (push, 0);

   BEGIN_NV04(push, NV50_3D(CLIP_RECTS_EN), 1);
   PUSH_DATA (push, 0);
   BEGIN_NV04(push, NV50_3D(CLIP_RECTS_MODE), 1);
   PUSH_DATA (push, NV50_3D_CLIP_RECTS_MODE_INSIDE_ANY);
   BEGIN_NV04(push, NV50_3D(CLIP_RECT_HORIZ(0)), 8 * 2);
   for (i = 0; i < 8 * 2; ++i)
      PUSH_DATA(push, 0);
   BEGIN_NV04(push, NV50_3D(CLIPID_ENABLE), 1);
   PUSH_DATA (push, 0);

   BEGIN_NV04(push, NV50_3D(VIEWPORT_TRANSFORM_EN), 1);
   PUSH_DATA (push, 1);
   for (i = 0; i < NV50_MAX_VIEWPORTS; i++) {
      BEGIN_NV04(push, NV50_3D(DEPTH_RANGE_NEAR(i)), 2);
      PUSH_DATAf(push, 0.0f);
      PUSH_DATAf(push, 1.0f);
      BEGIN_NV04(push, NV50_3D(VIEWPORT_HORIZ(i)), 2);
      PUSH_DATA (push, 8192 << 16);
      PUSH_DATA (push, 8192 << 16);
   }

   BEGIN_NV04(push, NV50_3D(VIEW_VOLUME_CLIP_CTRL), 1);
#ifdef NV50_SCISSORS_CLIPPING
   PUSH_DATA (push, 0x0000);
#else
   PUSH_DATA (push, 0x1080);
#endif

   BEGIN_NV04(push, NV50_3D(CLEAR_FLAGS), 1);
   PUSH_DATA (push, NV50_3D_CLEAR_FLAGS_CLEAR_RECT_VIEWPORT);

   /* We use scissors instead of exact view volume clipping,
    * so they're always enabled.
    */
   for (i = 0; i < NV50_MAX_VIEWPORTS; i++) {
      BEGIN_NV04(push, NV50_3D(SCISSOR_ENABLE(i)), 3);
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 8192 << 16);
      PUSH_DATA (push, 8192 << 16);
   }

   BEGIN_NV04(push, NV50_3D(RASTERIZE_ENABLE), 1);
   PUSH_DATA (push, 1);
   BEGIN_NV04(push, NV50_3D(POINT_RASTER_RULES), 1);
   PUSH_DATA (push, NV50_3D_POINT_RASTER_RULES_OGL);
   BEGIN_NV04(push, NV50_3D(FRAG_COLOR_CLAMP_EN), 1);
   PUSH_DATA (push, 0x11111111);
   BEGIN_NV04(push, NV50_3D(EDGEFLAG), 1);
   PUSH_DATA (push, 1);

   BEGIN_NV04(push, NV50_3D(VB_ELEMENT_BASE), 1);
   PUSH_DATA (push, 0);
   if (screen->base.class_3d >= NV84_3D_CLASS) {
      BEGIN_NV04(push, NV84_3D(VERTEX_ID_BASE), 1);
      PUSH_DATA (push, 0);
   }

   BEGIN_NV04(push, NV50_3D(UNK0FDC), 1);
   PUSH_DATA (push, 1);
   BEGIN_NV04(push, NV50_3D(UNK19C0), 1);
   PUSH_DATA (push, 1);

   PUSH_KICK (push);
}

static int
nv50_tls_alloc(struct nv50_context *nv50, unsigned tls_space,
               uint64_t *tls_size)
{
   struct nv50_screen *screen = nv50->screen;
   struct nouveau_device *dev = nv50->screen->base.device;
   int ret;

   nv50->cur_tls_space = util_next_power_of_two(tls_space / ONE_TEMP_SIZE) *
         ONE_TEMP_SIZE;
   if (nouveau_mesa_debug)
      debug_printf("allocating space for %u temps\n",
            util_next_power_of_two(tls_space / ONE_TEMP_SIZE));
   *tls_size = nv50->cur_tls_space * util_next_power_of_two(screen->TPs) *
         screen->MPsInTP * LOCAL_WARPS_ALLOC * THREADS_IN_WARP;

   ret = nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 1 << 16,
                        *tls_size, NULL, &nv50->tls_bo);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate local bo: %d\n", ret);
      return ret;
   }

   return 0;
}

int nv50_tls_realloc(struct nv50_context *nv50, unsigned tls_space)
{
   struct nv50_screen *screen = nv50->screen;
   struct nouveau_pushbuf *push = nv50->base.pushbuf;
   int ret;
   uint64_t tls_size;

   if (tls_space < nv50->cur_tls_space)
      return 0;
   if (tls_space > screen->max_tls_space) {
      /* fixable by limiting number of warps (LOCAL_WARPS_LOG_ALLOC /
       * LOCAL_WARPS_NO_CLAMP) */
      NOUVEAU_ERR("Unsupported number of temporaries (%u > %u). Fixable if someone cares.\n",
            (unsigned)(tls_space / ONE_TEMP_SIZE),
            (unsigned)(screen->max_tls_space / ONE_TEMP_SIZE));
      return -ENOMEM;
   }

   nouveau_bo_ref(NULL, &nv50->tls_bo);
   ret = nv50_tls_alloc(nv50, tls_space, &tls_size);
   if (ret)
      return ret;

   BEGIN_NV04(push, NV50_3D(LOCAL_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, nv50->tls_bo->offset);
   PUSH_DATA (push, nv50->tls_bo->offset);
   PUSH_DATA (push, util_logbase2(nv50->cur_tls_space / 8));

   return 1;
}

struct pipe_context *
nv50_create(struct pipe_screen *pscreen, void *priv, unsigned ctxflags)
{
   struct nv50_screen *screen = nv50_screen(pscreen);
   struct nv50_context *nv50;
   struct nouveau_device *dev = screen->base.device;
   struct nouveau_object *chan;
   struct pipe_context *pipe;
   unsigned stack_size;
   int ret;
   uint32_t flags;

   nv50 = CALLOC_STRUCT(nv50_context);
   if (!nv50)
      return NULL;
   pipe = &nv50->base.pipe;

   nouveau_context_init(&nv50->base);
   chan = nv50->base.channel;

   nv50->base.pushbuf->user_priv = nv50;
   nv50->base.pushbuf->rsvd_kick = 5;

   ret = nouveau_bo_new(dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP, 0, 4096,
                        NULL, &nv50->fence.bo);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate fence bo: %d\n", ret);
      goto out_err;
   }

   nouveau_bo_map(nv50->fence.bo, 0, NULL);
   nv50->fence.map = nv50->fence.bo->map;
   nv50->base.fence.emit = nv50_context_fence_emit;
   nv50->base.fence.update = nv50_context_fence_update;

   ret = nouveau_object_new(chan, 0xbeef0301, NOUVEAU_NOTIFIER_CLASS,
                            &(struct nv04_notify){ .length = 32 },
                            sizeof(struct nv04_notify), &nv50->sync);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate notifier: %d\n", ret);
      goto out_err;
   }

   ret = nouveau_object_new(chan, 0xbeef5039, NV50_M2MF_CLASS,
                            NULL, 0, &nv50->m2mf);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate PGRAPH context for M2MF: %d\n", ret);
      goto out_err;
   }

   ret = nouveau_object_new(chan, 0xbeef502d, NV50_2D_CLASS,
                            NULL, 0, &nv50->eng2d);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate PGRAPH context for 2D: %d\n", ret);
      goto out_err;
   }

   ret = nouveau_object_new(chan, 0xbeef5097, screen->base.class_3d,
                            NULL, 0, &nv50->tesla);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate PGRAPH context for 3D: %d\n", ret);
      goto out_err;
   }

   /* This over-allocates by a page. The GP, which would execute at the end of
    * the last page, would trigger faults. The going theory is that it
    * prefetches up to a certain amount.
    */
   ret = nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 1 << 16,
                        (3 << NV50_CODE_BO_SIZE_LOG2) + 0x1000,
                        NULL, &nv50->code);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate code bo: %d\n", ret);
      goto out_err;
   }

   nouveau_heap_init(&nv50->vp_code_heap, 0, 1 << NV50_CODE_BO_SIZE_LOG2);
   nouveau_heap_init(&nv50->gp_code_heap, 0, 1 << NV50_CODE_BO_SIZE_LOG2);
   nouveau_heap_init(&nv50->fp_code_heap, 0, 1 << NV50_CODE_BO_SIZE_LOG2);

   stack_size = util_next_power_of_two(screen->TPs) * screen->MPsInTP *
         STACK_WARPS_ALLOC * 64 * 8;

   ret = nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 1 << 16, stack_size, NULL,
                        &nv50->stack_bo);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate stack bo: %d\n", ret);
      goto out_err;
   }

   uint64_t tls_size;
   unsigned tls_space = 4/*temps*/ * ONE_TEMP_SIZE;
   ret = nv50_tls_alloc(nv50, tls_space, &tls_size);
   if (ret)
      goto out_err;

   if (nouveau_mesa_debug)
      debug_printf("TPs = %u, MPsInTP = %u, VRAM = %"PRIu64" MiB, tls_size = %"PRIu64" KiB\n",
            screen->TPs, screen->MPsInTP, dev->vram_size >> 20, tls_size >> 10);

   ret = nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 1 << 16, 4 << 16, NULL,
                        &nv50->uniforms);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate uniforms bo: %d\n", ret);
      goto out_err;
   }

   ret = nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 1 << 16, 3 << 16, NULL,
                        &nv50->txc);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate TIC/TSC bo: %d\n", ret);
      goto out_err;
   }

   nv50_context_init_hwctx(nv50);

   ret = nv50_context_compute_setup(nv50, nv50->base.pushbuf);
   if (ret) {
      NOUVEAU_ERR("Failed to init compute context: %d\n", ret);
      goto out_err;
   }

   nouveau_fence_new(&nv50->base, &nv50->base.fence.current);

   if (!nv50_blitctx_create(nv50))
      goto out_err;

   nv50->base.client = screen->base.client;

   ret = nouveau_bufctx_new(screen->base.client, 2, &nv50->bufctx);
   if (!ret)
      ret = nouveau_bufctx_new(screen->base.client, NV50_BIND_3D_COUNT,
                               &nv50->bufctx_3d);
   if (!ret)
      ret = nouveau_bufctx_new(screen->base.client, NV50_BIND_CP_COUNT,
                               &nv50->bufctx_cp);
   if (ret)
      goto out_err;

   nv50->base.screen    = &screen->base;
   nv50->base.copy_data = nv50_m2mf_copy_linear;
   nv50->base.push_data = nv50_sifc_linear_u8;
   nv50->base.push_cb   = nv50_cb_push;

   nv50->screen = screen;
   pipe->screen = pscreen;
   pipe->priv = priv;
   pipe->stream_uploader = u_upload_create_default(pipe);
   if (!pipe->stream_uploader)
      goto out_err;
   pipe->const_uploader = pipe->stream_uploader;

   pipe->destroy = nv50_destroy;

   pipe->draw_vbo = nv50_draw_vbo;
   pipe->clear = nv50_clear;
   pipe->launch_grid = nv50_launch_grid;

   pipe->flush = nv50_flush;
   pipe->texture_barrier = nv50_texture_barrier;
   pipe->memory_barrier = nv50_memory_barrier;
   pipe->get_sample_position = nv50_context_get_sample_position;
   pipe->emit_string_marker = nv50_emit_string_marker;

   if (!screen->cur_ctx) {
      /* Restore the last context's state here, normally handled during
       * context switch
       */
      nv50->state = screen->save_state;
      screen->cur_ctx = nv50;
      nouveau_pushbuf_bufctx(nv50->base.pushbuf, nv50->bufctx);
   }
   nv50->base.pushbuf->kick_notify = nv50_default_kick_notify;

   nv50_init_query_functions(nv50);
   nv50_init_surface_functions(nv50);
   nv50_init_state_functions(nv50);
   nv50_init_resource_functions(pipe);

   nv50->base.invalidate_resource_storage = nv50_invalidate_resource_storage;

   if (screen->base.device->chipset < 0x84 ||
       debug_get_bool_option("NOUVEAU_PMPEG", false)) {
      /* PMPEG */
      nouveau_context_init_vdec(&nv50->base);
   } else if (screen->base.device->chipset < 0x98 ||
              screen->base.device->chipset == 0xa0) {
      /* VP2 */
      pipe->create_video_codec = nv84_create_decoder;
      pipe->create_video_buffer = nv84_video_buffer_create;
   } else {
      /* VP3/4 */
      pipe->create_video_codec = nv98_create_decoder;
      pipe->create_video_buffer = nv98_video_buffer_create;
   }

   flags = NOUVEAU_BO_VRAM | NOUVEAU_BO_RD;

   BCTX_REFN_bo(nv50->bufctx_3d, 3D_SCREEN, flags, nv50->code);
   BCTX_REFN_bo(nv50->bufctx_3d, 3D_SCREEN, flags, nv50->uniforms);
   BCTX_REFN_bo(nv50->bufctx_3d, 3D_SCREEN, flags, nv50->txc);
   BCTX_REFN_bo(nv50->bufctx_3d, 3D_SCREEN, flags, nv50->stack_bo);
   if (nv50->compute) {
      BCTX_REFN_bo(nv50->bufctx_cp, CP_SCREEN, flags, nv50->code);
      BCTX_REFN_bo(nv50->bufctx_cp, CP_SCREEN, flags, nv50->txc);
      BCTX_REFN_bo(nv50->bufctx_cp, CP_SCREEN, flags, nv50->stack_bo);
   }

   flags = NOUVEAU_BO_GART | NOUVEAU_BO_WR;

   BCTX_REFN_bo(nv50->bufctx_3d, 3D_SCREEN, flags, nv50->fence.bo);
   BCTX_REFN_bo(nv50->bufctx, FENCE, flags, nv50->fence.bo);
   if (nv50->compute)
      BCTX_REFN_bo(nv50->bufctx_cp, CP_SCREEN, flags, nv50->fence.bo);

   nv50->base.scratch.bo_size = 2 << 20;

   util_dynarray_init(&nv50->global_residents, NULL);

   return pipe;

out_err:
   if (pipe->stream_uploader)
      u_upload_destroy(pipe->stream_uploader);
   if (nv50->bufctx_3d)
      nouveau_bufctx_del(&nv50->bufctx_3d);
   if (nv50->bufctx_cp)
      nouveau_bufctx_del(&nv50->bufctx_cp);
   if (nv50->bufctx)
      nouveau_bufctx_del(&nv50->bufctx);
   FREE(nv50->blit);
   FREE(nv50);
   return NULL;
}

void
nv50_bufctx_fence(struct nouveau_bufctx *bufctx,
                  struct nouveau_context *context, bool on_flush)
{
   struct nouveau_list *list = on_flush ? &bufctx->current : &bufctx->pending;
   struct nouveau_list *it;

   for (it = list->next; it != list; it = it->next) {
      struct nouveau_bufref *ref = (struct nouveau_bufref *)it;
      struct nv04_resource *res = ref->priv;
      if (res)
         nv50_resource_validate(res, context, (unsigned)ref->priv_data);
   }
}

static void
nv50_context_get_sample_position(struct pipe_context *pipe,
                                 unsigned sample_count, unsigned sample_index,
                                 float *xy)
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
      return; /* bad sample count -> undefined locations */
   }
   xy[0] = ptr[sample_index][0] * 0.0625f;
   xy[1] = ptr[sample_index][1] * 0.0625f;
}
