#include <stdbool.h>
#include <stdint.h>

enum nouv_winsys_param {
   NOUV_WINSYS_PARAM_CHIPSET,
   NOUV_WINSYS_PARAM_GART_SIZE,
   NOUV_WINSYS_PARAM_PCI_DEVICE,
   NOUV_WINSYS_PARAM_VRAM_SIZE,
};

struct nouv_winsys {
   /* func pointers */
   void (*destroy)(struct nouv_winsys *);
   bool (*get_param)(struct nouv_winsys *, enum nouv_winsys_param, uint64_t *val);
};

struct nouv_winsys *nouv_nouveau_winsys_create(int fd);
