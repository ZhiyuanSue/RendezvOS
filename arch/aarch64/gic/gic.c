#include <arch/aarch64/gic/gic.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <arch/aarch64/gic/gic_v3.h>
#include <modules/dtb/dev_tree.h>
#include <modules/log/log.h>
#include <common/string.h>

struct gic_chip gic;

struct gic_dt_driver {
        const char *compatible;
        error_t (*setup)(struct device_node *node);
};

/*
 * First DT hit wins. List GICv2 before gic-v3 so odd firmware that exposes
 * both still prefers a working driver. setup() fills @c gic (including
 * version); gic-v3 setup only fails.
 */
static const struct gic_dt_driver gic_dt_drivers[] = {
        {"arm,cortex-a15-gic", gic_v2_setup},
        {"arm,cortex-a7-gic", gic_v2_setup},
        {"arm,cortex-a9-gic", gic_v2_setup},
        {"arm,gic-400", gic_v2_setup},
        {"arm,arm11mp-gic", gic_v2_setup},
        {"arm,gic-v3", gic_v3_setup},
};

error_t gic_probe(void)
{
        memset(&gic, 0, sizeof(gic));

        for (size_t i = 0;
             i < sizeof(gic_dt_drivers) / sizeof(struct gic_dt_driver);
             i++) {
                const struct gic_dt_driver *drv = &gic_dt_drivers[i];
                struct device_node *node = dev_node_find_by_compatible(
                        NULL, (char *)drv->compatible);

                if (!node)
                        continue;

                print("[ GIC ] DT match \"%s\"\n", drv->compatible);
                error_t e = drv->setup(node);
                if (e != REND_SUCCESS) {
                        print("[ GIC ] setup failed for \"%s\" (%d)\n",
                              drv->compatible,
                              (int)e);
                        memset(&gic, 0, sizeof(gic));
                        return e;
                }
                if (!gic_ready()) {
                        print("[ GIC ] ops incomplete after \"%s\"\n",
                              drv->compatible);
                        memset(&gic, 0, sizeof(gic));
                        return -E_RENDEZVOS;
                }
                print("[ GIC ] bound version %d\n", (int)gic.version);
                return REND_SUCCESS;
        }

        print("[ GIC ] no supported interrupt-controller in DT\n");
        return -E_REND_NOFOUND;
}
