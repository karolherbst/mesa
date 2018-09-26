#ifndef __NVC0_SCREEN_H__
#define __NVC0_SCREEN_H__

#include "nouveau_screen.h"
#include "nouveau_context.h"
#include "nouveau_mm.h"
#include "nouveau_fence.h"
#include "nouveau_heap.h"

#include "nv_object.xml.h"

#include "nvc0/nvc0_winsys.h"
#include "nvc0/nvc0_stateobj.h"

#define NVC0_TIC_MAX_ENTRIES 2048
#define NVC0_TSC_MAX_ENTRIES 2048
#define NVE4_IMG_MAX_HANDLES 512

/* doesn't count driver-reserved slot */
#define NVC0_MAX_PIPE_CONSTBUFS 15
#define NVC0_MAX_CONST_BUFFERS  16
#define NVC0_MAX_CONSTBUF_SIZE  65536

#define NVC0_MAX_SURFACE_SLOTS 16

#define NVC0_MAX_VIEWPORTS 16

#define NVC0_MAX_BUFFERS 32

#define NVC0_MAX_IMAGES 8

#define NVC0_MAX_WINDOW_RECTANGLES 8

struct nvc0_context;

struct nvc0_blitter;

struct nvc0_cb_binding {
   uint64_t addr;
   int size;
};

struct nvc0_screen {
   struct nouveau_screen base;

   struct nvc0_context *cur_ctx;

   int num_occlusion_queries_active;

   uint8_t gpc_count;
   uint16_t mp_count;
   uint16_t mp_count_compute; /* magic reg can make compute use fewer MPs */

   struct {
      struct nvc0_program *prog; /* compute state object to read MP counters */
      struct nvc0_hw_sm_query *mp_counter[8]; /* counter to query allocation */
      uint8_t num_hw_sm_active[2];
      bool mp_counters_enabled;
   } pm;

};

static inline struct nvc0_screen *
nvc0_screen(struct pipe_screen *screen)
{
   return (struct nvc0_screen *)screen;
}

int nvc0_screen_get_driver_query_info(struct pipe_screen *, unsigned,
                                      struct pipe_driver_query_info *);

int nvc0_screen_get_driver_query_group_info(struct pipe_screen *, unsigned,
                                            struct pipe_driver_query_group_info *);

void nvc0_screen_make_buffers_resident(struct nvc0_screen *);

int nvc0_get_compute_class(struct nvc0_screen *screen);
int nve4_get_compute_class(struct nvc0_screen *screen);

static inline void
nvc0_resource_fence(struct nv04_resource *res, struct nouveau_context *context,
                    uint32_t flags)
{
   if (res->mm) {
      nouveau_fence_ref(context->fence.current, &res->fence);
      if (flags & NOUVEAU_BO_WR)
         nouveau_fence_ref(context->fence.current, &res->fence_wr);
   }
}

static inline void
nvc0_resource_validate(struct nv04_resource *res,
                       struct nouveau_context *context, uint32_t flags)
{
   if (likely(res->bo)) {
      if (flags & NOUVEAU_BO_WR)
         res->status |= NOUVEAU_BUFFER_STATUS_GPU_WRITING |
            NOUVEAU_BUFFER_STATUS_DIRTY;
      if (flags & NOUVEAU_BO_RD)
         res->status |= NOUVEAU_BUFFER_STATUS_GPU_READING;

      nvc0_resource_fence(res, context, flags);
   }
}

struct nvc0_format {
   uint32_t rt;
   struct {
      unsigned format:7;
      unsigned type_r:3;
      unsigned type_g:3;
      unsigned type_b:3;
      unsigned type_a:3;
      unsigned src_x:3;
      unsigned src_y:3;
      unsigned src_z:3;
      unsigned src_w:3;
   } tic;
   uint32_t usage;
};

struct nvc0_vertex_format {
   uint32_t vtx;
   uint32_t usage;
};

extern const struct nvc0_format nvc0_format_table[];
extern const struct nvc0_vertex_format nvc0_vertex_format[];

#endif
