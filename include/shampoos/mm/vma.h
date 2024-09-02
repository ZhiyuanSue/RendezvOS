#ifndef _SHAMPOOS_VMA_H_
#define _SHAMPOOS_VMA_H_
#include <common/types.h>
#include <common/dsa/rb_tree.h>

struct vma {
        struct rb_root rb_tree;
};

#endif