#include <arch/aarch64/gic/gic.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <arch/aarch64/gic/gic_v3.h>
#include <modules/dtb/dev_tree.h>
#include <modules/log/log.h>
#include <common/string.h>

struct gic_chip gic;

struct gic_dt_driver {
        const char *compatible;
        error_t (*bind)(struct device_node *node);
};

/*
 * First DT hit wins. List GICv2 before gic-v3 so odd firmware that exposes
 * both still prefers a working driver. bind() maps MMIO and fills @c gic
 * (including version); HW bring-up is gic.init_* later. gic-v3 bind fails.
 */
static const struct gic_dt_driver gic_dt_drivers[] = {
        {"arm,cortex-a15-gic", gic_v2_bind},
        {"arm,cortex-a7-gic", gic_v2_bind},
        {"arm,cortex-a9-gic", gic_v2_bind},
        {"arm,gic-400", gic_v2_bind},
        {"arm,arm11mp-gic", gic_v2_bind},
        {"arm,gic-v3", gic_v3_bind},
        {/* sentinel */},
};

error_t gic_probe(void)
{
        for (const struct gic_dt_driver *drv = gic_dt_drivers; drv->compatible;
             drv++) {
                struct device_node *node = dev_node_find_by_compatible(
                        NULL, (char *)drv->compatible);

                if (!node)
                        continue;

                print("[ GIC ] DT match \"%s\"\n", drv->compatible);
                error_t e = drv->bind(node);
                if (e != REND_SUCCESS) {
                        print("[ GIC ] bind failed for \"%s\" with error code %d\n",
                              drv->compatible,
                              (int)e);
                        memset(&gic, 0, sizeof(gic));
                        return e;
                }
                /* bind success ⇒ driver's static chip table installed. */
                print("[ GIC ] bound version %d\n", (int)gic.version);
                return REND_SUCCESS;
        }

        print("[ GIC ] no supported interrupt-controller in DT\n");
        return -E_REND_NOFOUND;
}
