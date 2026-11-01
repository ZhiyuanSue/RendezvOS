#ifndef _RENDEZVOS_GIC_V3_H_
#define _RENDEZVOS_GIC_V3_H_

#include <arch/aarch64/gic/gic.h>
#include <modules/dtb/dev_tree.h>

/*
 * GICv3: not implemented.
 *
 * @c gic_probe may match "arm,gic-v3"; @c gic_v3_setup returns @c -E_RENDEZVOS
 * (no Redistributor / IROUTER / ITS). Callers use only @c gic_chip in gic.h.
 */

error_t gic_v3_setup(struct device_node *node);

#endif /* _RENDEZVOS_GIC_V3_H_ */
