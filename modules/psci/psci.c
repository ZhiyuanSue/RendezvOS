#include <modules/psci/psci.h>

static int psci_call_type;
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
                psci_call_type = psci_call_msc;
                pr_info("[ PSCI ] smc call\n");
        } else if (!strcmp(method_name, method_hvc)) {
                psci_call_type = psci_call_hvc;
                pr_info("[ PSCI ] hvc call\n");
        } else {
                pr_error("[ PSCI ] unknown psci call method\n");
                return;
        }
}