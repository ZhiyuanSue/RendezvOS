#include <arch/aarch64/gic/gic_v3.h>
#include <modules/log/log.h>

error_t gic_v3_setup(struct device_node *node)
{
        (void)node;
        print("[ GIC ] GICv3 DT match, no driver "
              "(Redistributor/IROUTER); need a GICv2-compatible DT\n");
        return -E_RENDEZVOS;
}
