#ifndef _RENDEZVOS_MM_ANON_BACKEND_H_
#define _RENDEZVOS_MM_ANON_BACKEND_H_

#include <common/mm.h>
#include <common/types.h>

struct VSpace;

/*
    just a tmp realization of anon backend
 */
vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags);

#endif
