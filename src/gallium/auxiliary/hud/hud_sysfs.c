/**************************************************************************
 *
 * Copyright 2016 Karol Herbst <nouveau@karolherbst.de>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/* This file contains code for reading various information out of sysfs, like
   temperature and power consumption
 */

#include "hud/hud_private.h"

#include "loader.h"
#include "gallium/drivers/nouveau/nouveau_screen.h"
#include "os/os_time.h"

#include <fts.h>

#include <sys/stat.h>

struct hud_sysfs_data {
   char file_path[PATH_MAX];
   uint64_t last_time;
};

static void get_hwmon_root(char root[PATH_MAX], struct pipe_screen *pscreen)
{
   char *sysfs_device_path;
   char buf[PATH_MAX] = { 0 };
   struct stat st;
   int fd;

   if (!pscreen->get_fd)
      return;

   fd = pscreen->get_fd(pscreen);
   sysfs_device_path = loader_get_sysfs_path_for_fd(fd);

   sprintf(buf, "%s/hwmon", sysfs_device_path);

   if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
      char *const dirs[] = { buf, 0 };
      FTS *ftsp = fts_open(dirs, FTS_NOCHDIR | FTS_NOSTAT, NULL);
      FTSENT *p;
      while ((p = fts_read(ftsp)) != NULL) {
         if (p->fts_level != 1)
            continue;
         switch (p->fts_info) {
         case FTS_D:
            strcpy(root, p->fts_path);
            break;
         }
      }
      fts_close(ftsp);
   }

   free(sysfs_device_path);
}

void hud_supports_sysfs(struct pipe_screen *pscreen, struct sysfs_support *s)
{
   char root[PATH_MAX] = { '\0' };
   char buf[PATH_MAX];
   get_hwmon_root(root, pscreen);

   if (root[0] != '\0') {
      sprintf(buf, "%s/temp1_input", root);
      s->hwmon.temp = access(buf, R_OK) == 0 ? true : false;
      sprintf(buf, "%s/power1_input", root);
      s->hwmon.power = access(buf, R_OK) == 0 ? true : false;
   } else {
      s->hwmon.temp = false;
      s->hwmon.power = false;
   }
}

static void
query_data(struct hud_graph *gr)
{
   struct hud_sysfs_data *data = gr->query_data;
   uint64_t now = os_time_get();

   if (data->last_time) {
      if (data->last_time + gr->pane->period <= now) {
         FILE *f = fopen(data->file_path, "r");
         uint64_t value;

         if (!f)
            return;

         fscanf(f, "%li", &value);
         fclose(f);

         data->last_time = now;
         hud_graph_add_value(gr, ((float)value) / 1000);
      }
   } else {
      data->last_time = now;
   }
}

static void
free_query_data(void *p)
{
   FREE(p);
}

void
hud_sysfs_hwmon_temp_install(struct hud_pane *pane, struct pipe_screen *pscreen)
{
   struct hud_graph *gr;
   struct hud_sysfs_data *data;
   char path[PATH_MAX], emer_path[PATH_MAX];
   FILE *f;
   uint64_t value;

   gr = CALLOC_STRUCT(hud_graph);
   if (!gr)
      return;

   strcpy(gr->name, "temperature °C");

   data = CALLOC_STRUCT(hud_sysfs_data);
   if (!data) {
      FREE(gr);
      return;
   }

   get_hwmon_root(path, pscreen);
   sprintf(data->file_path, "%s/temp1_input", path);
   sprintf(emer_path, "%s/temp1_emergency", path);

   gr->query_data = data;
   gr->query_new_value = query_data;
   gr->free_query_data = free_query_data;

   hud_pane_add_graph(pane, gr);

   f = fopen(emer_path, "r");

   if (f) {
      fscanf(f, "%li", &value);
      fclose(f);
      hud_pane_set_max_value(pane, value / 1000);
   }
}

void
hud_sysfs_hwmon_power_install(struct hud_pane *pane, struct pipe_screen *pscreen)
{
   struct hud_graph *gr;
   struct hud_sysfs_data *data;
   char path[PATH_MAX];

   gr = CALLOC_STRUCT(hud_graph);
   if (!gr)
      return;

   strcpy(gr->name, "power consumption mW");

   data = CALLOC_STRUCT(hud_sysfs_data);
   if (!data) {
      FREE(gr);
      return;
   }

   get_hwmon_root(path, pscreen);
   sprintf(data->file_path, "%s/power1_input", path);

   gr->query_data = data;
   gr->query_new_value = query_data;
   gr->free_query_data = free_query_data;

   hud_pane_add_graph(pane, gr);
}
