#include <modules/psci/psci.h>

struct psci_func_64 psci_func;

static int psci_call_type;
static u64 (*psci_call_func)(u64 func_id, u64 arg1, u64 arg2, u64 arg3);

extern u64 psci_smc(u64 func_id, u64 arg1, u64 arg2, u64 arg3);
extern u64 psci_hvc(u64 func_id, u64 arg1, u64 arg2, u64 arg3);

void psci_print_version()
{
        u32 psci_ver = psci_version();
        u32 psci_major_ver = psci_ver >> 16;
        u32 psci_minor_ver = psci_ver & 0xffff;
        printk("[ PSCI ] version is ", LOG_OFF);
        if (psci_major_ver == 1) {
                if (psci_minor_ver == 0) {
                        printk("C\n", LOG_OFF);
                } else if (psci_minor_ver == 1) {
                        printk("D\n", LOG_OFF);
                } else if (psci_minor_ver == 2) {
                        printk("E\n", LOG_OFF);
                } else if (psci_minor_ver == 3) {
                        printk("F\n", LOG_OFF);
                } else {
                        printk("error version\n", LOG_OFF);
                }
        } else if (psci_major_ver == 0 && psci_minor_ver == 2) {
                printk("B\n", LOG_OFF);
        } else {
                printk("error version\n", LOG_OFF);
        }
}
void psci_init(void)
{
        char* compatible = "arm,psci";
        char* method = "method";
        char* method_smc = "smc";
        char* method_hvc = "hvc";
        char* method_name;
        char* migrate_char = "migrate";
        char* cpu_on_char = "cpu_on";
        char* cpu_off_char = "cpu_off";
        char* cpu_suspend_char = "cpu_suspend";
        struct device_node* node =
                dev_node_find_by_compatible(NULL, compatible);
        if (!node) {
                printk("[ PSCI ] cannot find psci node in dtb\n", LOG_OFF);
                psci_func.enable = false;
                return;
        }
        struct property* prop = dev_node_find_property(node, method, 7);
        if (!prop) {
                printk("[ PSCI ] cannot find method property in this node\n",
                       LOG_OFF);
                psci_func.enable = false;
                return;
        }
        if (property_read_string(prop, &method_name)) {
                printk("[ PSCI ] property read string error\n", LOG_OFF);
                psci_func.enable = false;
                return;
        }
        if (!strcmp(method_name, method_smc)) {
                psci_call_type = psci_call_smc;
                psci_call_func = psci_smc;
                printk("[ PSCI ] use smc call\n", LOG_OFF);
        } else if (!strcmp(method_name, method_hvc)) {
                psci_call_type = psci_call_hvc;
                psci_call_func = psci_hvc;
                printk("[ PSCI ] use hvc call\n", LOG_OFF);
        } else {
                printk("[ PSCI ] unknown psci call method\n", LOG_OFF);
                psci_func.enable = false;
                return;
        }
        psci_func.enable = true;
        psci_func.version = psci_version;
        psci_func.system_off = psci_system_off;
        psci_func.system_reset = psci_system_reset;

        prop = dev_node_find_property(node, migrate_char, 8);
        if (!prop) {
                printk("[ PSCI ] cannot find migrate property in this node\n",
                       LOG_OFF);
                return;
        } else {
                psci_func.migrate = psci_migrate_64;
        }

        prop = dev_node_find_property(node, cpu_on_char, 7);
        if (!prop) {
                printk("[ PSCI ] cannot find cpu on property in this node\n",
                       LOG_OFF);
                return;
        } else {
                psci_func.cpu_on = psci_cpu_on_64;
        }

        prop = dev_node_find_property(node, cpu_off_char, 8);
        if (!prop) {
                printk("[ PSCI ] cannot find cpu off property in this node\n",
                       LOG_OFF);
                return;
        } else {
                psci_func.cpu_off = psci_cpu_off;
        }

        prop = dev_node_find_property(node, cpu_suspend_char, 12);
        if (!prop) {
                printk("[ PSCI ] cannot find cpu suspend property in this node\n",
                       LOG_OFF);
                return;
        } else {
                psci_func.cpu_suspend_64 = psci_cpu_suspend_64;
        }

        psci_print_version();
}
u32 psci_version(void)
{
        u32 version = -psci_not_support;
        version = (u32)psci_call_func(psci_version_func_id, 0, 0, 0);
        return version;
}
i32 psci_cpu_off(void)
{
        u32 is_denied = -psci_denied;
        is_denied = psci_call_func(psci_cpu_off_func_id, 0, 0, 0);
        return is_denied;
}

i64 psci_cpu_on_64(u64 target_cpu, u64 entry_point_address, u64 context_id)
{
        i64 res = 0;
        res = psci_call_func(psci_64_cpu_on_func_id,
                             target_cpu,
                             entry_point_address,
                             context_id);
        return res;
}

i64 psci_cpu_suspend_64(u32 power_state, u64 entry_point_address,
                        u32 context_id)
{
        i64 res = 0;
        res = psci_call_func(psci_64_cpu_suspend_func_id,
                             power_state,
                             entry_point_address,
                             context_id);
        return res;
}

i64 psci_affinity_info_64(u64 target_affinity, u32 lowest_affinity_level)
{
        i64 res = 0;
        res = psci_call_func(psci_64_affinity_info_func_id,
                             target_affinity,
                             lowest_affinity_level,
                             0);
        return res;
}

i64 psci_migrate_64(u64 target_cpu)
{
        i64 res = 0;
        res = psci_call_func(psci_64_migrate_func_id, target_cpu, 0, 0);
        return res;
}

i32 psci_migrate_info_type(void)
{
        i32 res = 0;
        res = psci_call_func(psci_migrate_info_type_func_id, 0, 0, 0);
        return res;
}

i64 psci_migrate_info_up_cpu_64(void)
{
        i64 res = 0;
        res = psci_call_func(psci_64_migrate_info_up_cpu_func_id, 0, 0, 0);
        return res;
}

void psci_system_off(void)
{
        psci_call_func(psci_system_off_func_id, 0, 0, 0);
}

i64 psci_system_off_2_64(u32 type, u64 cookie)
{
        i64 res = 0;
        res = psci_call_func(psci_64_system_off_2_func_id, type, cookie, 0);
        return res;
}

void psci_system_reset(void)
{
        psci_call_func(psci_system_reset_func_id, 0, 0, 0);
}

i64 psci_system_reset_2_64(u32 reset_type, u64 cookie)
{
        i64 res = 0;
        res = psci_call_func(
                psci_64_system_reset_2_func_id, reset_type, cookie, 0);
        return res;
}

i32 psci_mem_protect(u32 enable)
{
        i32 res = 0;
        res = psci_call_func(psci_mem_protect_func_id, enable, 0, 0);
        return res;
}

i64 psci_mem_protect_check_range_64(u64 base, u64 length)
{
        i64 res = 0;
        res = psci_call_func(
                psci_64_mem_protect_check_range_func_id, base, length, 0);
        return res;
}

i32 psci_feature(u32 psci_feature_id)
{
        i32 res = 0;
        res = psci_call_func(psci_feature_function_id, psci_feature_id, 0, 0);
        return res;
}

i32 psci_cpu_freeze(void)
{
        i32 res = 0;
        res = psci_call_func(psci_cpu_freeze_func_id, 0, 0, 0);
        return res;
}

i64 psci_cpu_default_suspend_64(u64 entry_point_address, u64 context_id)
{
        i64 res = 0;
        res = psci_call_func(psci_64_cpu_default_suspend_func_id,
                             entry_point_address,
                             context_id,
                             0);
        return res;
}

i64 psci_node_hw_state_64(u64 target_cpu, u32 power_level)
{
        i64 res = 0;
        res = psci_call_func(
                psci_64_node_hw_state_func_id, target_cpu, power_level, 0);
        return res;
}

i64 psci_system_suspend_64(u64 entry_point_address, u64 context_id)
{
        i64 res = 0;
        res = psci_call_func(psci_64_system_suspend_func_id,
                             entry_point_address,
                             context_id,
                             0);
        return res;
}

i32 psci_set_suspend_mode(u32 mode)
{
        i32 res = 0;
        res = psci_call_func(psci_set_suspend_mode_func_id, mode, 0, 0);
        return res;
}

u64 psci_state_residency_64(u64 target_cpu, u32 power_state)
{
        u64 res = 0;
        res = psci_call_func(
                psci_64_state_residency_func_id, target_cpu, power_state, 0);
        return res;
}

u64 psci_state_count_64(u64 target_cpu, u32 power_state)
{
        u64 res = 0;
        res = psci_call_func(
                psci_64_state_count_func_id, target_cpu, power_state, 0);
        return res;
}