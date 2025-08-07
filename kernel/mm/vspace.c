#include <rendezvos/mm/mm.h>

void init_vspace(VSpace* vs, u64 vspace_id, void* vspace_node)
{
        vs->vspace_lock = NULL;
        vs->vspace_id = vspace_id;
        vs->_vspace_node = vspace_node;
}
