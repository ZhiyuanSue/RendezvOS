#include <rendezvos/mm/map_handler.h>

void init_vspace(struct vspace* vs, paddr vspace_root_addr, uint64_t vspace_id)
{
        vs->vspace_lock = NULL;
        vs->vspace_root = vspace_root_addr;
}
