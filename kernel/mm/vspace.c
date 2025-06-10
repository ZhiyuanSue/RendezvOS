#include <rendezvos/mm/mm.h>

void init_vspace(VSpace* vs, paddr vspace_root_addr, uint64_t vspace_id,
                 void* vspace_node)
{
        vs->vspace_lock = NULL;
        vs->vspace_root_addr = vspace_root_addr;
        vs->vspace_id = vspace_id;
        vs->_vspace_node = vspace_node;
}
