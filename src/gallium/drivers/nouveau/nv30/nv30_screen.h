#ifndef __NV30_SCREEN_H__
#define __NV30_SCREEN_H__

#include <stdio.h>

#include "util/list.h"

#include "nouveau_debug.h"
#include "nouveau_screen.h"
#include "nouveau_fence.h"
#include "nouveau_heap.h"
#include "nv30/nv30_winsys.h"
#include "nv30/nv30_resource.h"

struct nv30_context;

struct nv30_screen {
   struct nouveau_screen base;

   struct nv30_context *cur_ctx;

   unsigned max_sample_count;
   unsigned eng3d_oclass;
};

static inline struct nv30_screen *
nv30_screen(struct pipe_screen *pscreen)
{
   return (struct nv30_screen *)pscreen;
}

#endif
