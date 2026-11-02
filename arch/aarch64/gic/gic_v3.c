#include <arch/aarch64/gic/gic_v3.h>
#include <modules/log/log.h>

error_t gic_v3_bind(struct device_node *node)
{
        (void)node;
        print("[ GIC ] GICv3 find, but not support yet\n");
        return -E_RENDEZVOS;
}
