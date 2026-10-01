#include <arch/aarch64/time.h>
#include <arch/aarch64/gic/gic_dt.h>
#include <arch/aarch64/sync/barrier.h>
#include <arch/aarch64/trap/trap.h>
#include <common/string.h>
#include <modules/dtb/dev_tree.h>
#include <modules/dtb/of.h>
#include <modules/dtb/property.h>
#include <modules/log/log.h>
#include <rendezvos/system/panic.h>
#include <rendezvos/time.h>
u64 time_irq_cycle;
u64 timer_freq;

static u32 timer_irq_num; /* 0 = not probed; IRQ trap ids are ≥ 64 */

static const struct of_device_id arch_timer_of_match[] = {
        { .compatible = "arm,armv8-timer" },
        {/*sentinel*/},
};

/**
 * @brief Resolve timer trap id: BSP reads DTB once; AP returns the cache.
 */
u32 arch_get_timer_irq_num(bool is_bsp)
{
        const struct of_device_id *match;
        const char *irq_prop_name;
        struct device_node *node;
        struct property *prop;
        u32 cells[12];
        u32 cell0, n_cells, type, ppi, intid;

        if (timer_irq_num)
                return timer_irq_num;

        if (!is_bsp)
                goto fail;

        node = of_find_matching_node(device_root, arch_timer_of_match, &match);
        if (!node || !match)
                goto fail;

        /* Binding order: pick non-secure physical (CNTP). */
        cell0 = ARCH_TIMER_NS_PHYS * 3u;
        n_cells = cell0 + 3u;

        irq_prop_name =
                property_types[PROPERTY_TYPE_INTERRUPT].property_string;
        prop = dev_node_find_property(node, (char *)irq_prop_name,
                                      (int)strlen(irq_prop_name));
        if (!prop || prop->len < (int)(n_cells * sizeof(u32)))
                goto fail;

        if (property_read_u32_arr(prop, cells, (int)n_cells))
                goto fail;

        type = cells[cell0];
        ppi = cells[cell0 + 1];
        if (type != GIC_DT_TYPE_PPI)
                goto fail;

        intid = GIC_V2_PPI_START + ppi;
        if (!gic_v2_is_ppi(intid))
                goto fail;

        timer_irq_num = AARCH64_IRQ_TO_TRAP_ID(intid);
        return timer_irq_num;

fail:
        kernel_panic("[ERROR] arch_get_timer_irq_num: probe failed");
}

/**
 * @brief Read CNTPCT_EL0 to get physical count.
 */
tick_t get_phy_cnt(void)
{
        tick_t cntpct_el0_val;
        isb();
        mrs("CNTPCT_EL0", cntpct_el0_val);
        return cntpct_el0_val;
}
/**
 * @brief Read CNTVCT_EL0 .
 */
tick_t get_virt_cnt(void)
{
        tick_t cntvct_el0_val;
        isb();
        mrs("CNTVCT_EL0", cntvct_el0_val);
        return cntvct_el0_val;
}
/**
 * @brief Read CNTV_CVAL_EL0.
 */
tick_t get_cval(void)
{
        tick_t cntv_cval;
        isb();
        mrs("CNTV_CVAL_EL0", cntv_cval);
        return cntv_cval;
}
/**
 * @brief Enable EL1 physical timer; BSP read the CNTFRQ and set time_irq_cycle.
 */
u64 arch_init_timer(bool is_bsp)
{
        u64 time_ctrl_value = CNTV_CTL_EL0_ENABLE;
        if (is_bsp) {
                /*here we use the compare value way*/
                mrs("CNTFRQ_EL0", timer_freq);
                time_irq_cycle = timer_freq / INT_PER_SECOND;
        }
        msr("CNTP_TVAL_EL0", time_irq_cycle);
        msr("CNTP_CTL_EL0", time_ctrl_value);
        return time_irq_cycle;
}
/**
 * @brief Reset CNTP_TVAL_EL0 with @p next_event_gap.
 */
void arch_reset_timer(u64 next_event_gap)
{
        msr("CNTP_TVAL_EL0", next_event_gap);
        isb();
}
tick_t arch_timer_read(void)
{
        return get_phy_cnt();
}
tick_t arch_timer_get_hz(void)
{
        return timer_freq;
}