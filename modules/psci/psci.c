#include <modules/psci/psci.h>

static int psci_call_type;
extern u64 psci_smc(u64 func_id, u64 arg1, u64 arg2, u64 arg3);
extern u64 psci_hvc(u64 func_id, u64 arg1, u64 arg2, u64 arg3);
void psci_print_version()
{
        u32 psci_ver = psci_version();
        u32 psci_major_ver = psci_ver >> 16;
        u32 psci_minor_ver = psci_ver & 0xffff;
        pr_info("[ PSCI ] version is ");
        if (psci_major_ver == 1) {
                if (psci_minor_ver == 0) {
                        pr_info("C\n");
                } else if (psci_minor_ver == 1) {
                        pr_info("D\n");
                } else if (psci_minor_ver == 2) {
                        pr_info("E\n");
                } else if (psci_minor_ver == 3) {
                        pr_info("F\n");
                } else {
                        pr_info("error version\n");
                }
        } else if (psci_major_ver == 0 && psci_minor_ver == 2) {
                pr_info("B\n");
        } else {
                pr_info("error version\n");
        }
}
void psci_init(void)
{
        char* compatible = "arm,psci";
        char* method = "method";
        char* method_smc = "smc";
        char* method_hvc = "hvc";
        char* method_name;
        struct device_node* node =
                dev_node_find_by_compatible(NULL, compatible);
        if (!node) {
                pr_error("[ PSCI ] cannot find psci node in dtb\n");
                return;
        }
        struct property* prop = dev_node_find_property(node, method, 7);
        if (!prop) {
                pr_error("[ PSCI ] cannot find method property in this node\n");
                return;
        }
        if (property_read_string(prop, &method_name)) {
                pr_error("[ PSCI ] property read string error\n");
                return;
        }
        if (!strcmp(method_name, method_smc)) {
                psci_call_type = psci_call_smc;
                pr_info("[ PSCI ] use smc call\n");
        } else if (!strcmp(method_name, method_hvc)) {
                psci_call_type = psci_call_hvc;
                pr_info("[ PSCI ] use hvc call\n");
        } else {
                pr_error("[ PSCI ] unknown psci call method\n");
                return;
        }
        psci_print_version();
}
u32 psci_version(void)
{
        u32 version = -psci_not_support;
        if (psci_call_type == psci_call_smc) {
                version = (u32)psci_smc(psci_version_func_id, 0, 0, 0);
        } else if (psci_call_type == psci_call_hvc) {
                version = (u32)psci_hvc(psci_version_func_id, 0, 0, 0);
        }
        return version;
}
i32 psci_cpu_off(void)
{
        u32 is_denied = -psci_denied;
        if (psci_call_type == psci_call_smc) {
                is_denied = psci_smc(psci_cpu_off_func_id, 0, 0, 0);
        } else if (psci_call_type == psci_call_hvc) {
                is_denied = psci_hvc(psci_cpu_off_func_id, 0, 0, 0);
        }
        return is_denied;
}
void psci_system_off(void)
{
        if (psci_call_type == psci_call_smc) {
                psci_smc(psci_system_off_function_id, 0, 0, 0);
        } else if (psci_call_type == psci_call_hvc) {
                psci_hvc(psci_system_off_function_id, 0, 0, 0);
        }
}

i64 psci_cpu_on_64(u64 target_cpu, u64 entry_point_address, u64 context_id)
{
        i64 res = 0;
        if (psci_call_type == psci_call_smc) {
                res = psci_smc(psci_64_cpu_on_func_id,
                               target_cpu,
                               entry_point_address,
                               context_id);
        } else if (psci_call_type == psci_call_hvc) {
                res = psci_hvc(psci_64_cpu_on_func_id,
                               target_cpu,
                               entry_point_address,
                               context_id);
        }
        return res;
}