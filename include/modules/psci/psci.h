#ifndef _SHAMPOOS_PSCI_H_
#define _SHAMPOOS_PSCI_H_

#include <common/types.h>
#include <modules/dtb/dtb.h>
#include <modules/log/log.h>
#include "psci_error.h"
/*although I think the 32 version will not be used, I still record it*/
enum psci_call_method { psci_call_none = 0, psci_call_smc, psci_call_hvc };
struct psci_func_64 {
        bool enable;
        u32 (*version)(void);
        i64 (*cpu_suspend_64)(u32 power_state, u64 entry_point_address,
                              u32 context_id);
        i32 (*cpu_off)(void);
        i64 (*cpu_on)(u64 target_cpu, u64 entry_point_address, u64 context_id);
        i64 (*affinity_info)(u64 target_affinity, u32 lowest_affinity_level);
        i64 (*migrate)(u64 target_cpu);
        i32 (*migrate_info_type)(void);
        i64 (*migrate_info_up_cpu)(void);
        void (*system_off)(void);
        i64 (*system_off_2)(u32 type, u64 cookie);
        void (*system_reset)(void);
        i64 (*system_reset_2)(u32 reset_type, u64 cookie);
        i32 (*mem_protect)(u32 enable);
        i64 (*mem_protect_check_range)(u64 base, u64 length);
        i32 (*feature)(u32 psci_feature_id);
        i32 (*cpu_freeze)(void);
        i64 (*cpu_default_suspend)(u64 entry_point_address, u64 context_id);
        i64 (*node_hw_state)(u64 target_cpu, u32 power_level);
        i64 (*system_suspend)(u64 entry_point_address, u64 context_id);
        i32 (*set_suspend_mode)(u32 mode);
        u64 (*state_residency)(u64 target_cpu, u32 power_state);
        u64 (*state_count)(u64 target_cpu, u32 power_state);
};
extern struct psci_func_64 psci_func;
void psci_init(void);

#define psci_version_func_id 0x84000000
u32 psci_version(void);

#define psci_32_cpu_suspend_func_id 0x84000001
i32 psci_cpu_suspend_32(u32 power_state, u32 entry_point_address,
                        u32 context_id);
#define psci_64_cpu_suspend_func_id 0xC4000001
i64 psci_cpu_suspend_64(u32 power_state, u64 entry_point_address,
                        u32 context_id);

#define psci_cpu_off_func_id 0x84000002
i32 psci_cpu_off(void);

#define psci_32_cpu_on_func_id 0x84000003
i32 psci_cpu_on_32(u32 target_cpu, u32 entry_point_address, u32 context_id);
#define psci_64_cpu_on_func_id 0xC4000003
i64 psci_cpu_on_64(u64 target_cpu, u64 entry_point_address, u64 context_id);

#define psci_32_affinity_info_func_id 0x84000004
i32 psci_affinity_info_32(u32 target_affinity, u32 lowest_affinity_level);
#define psci_64_affinity_info_func_id 0xC4000004
i64 psci_affinity_info_64(u64 target_affinity, u32 lowest_affinity_level);

#define psci_32_migrate_func_id 0x84000005
i32 psci_migrate_32(u32 target_cpu);
#define psci_64_migrate_func_id 0xC4000005
i64 psci_migrate_64(u64 target_cpu);

#define psci_migrate_info_type_func_id 0x84000006
i32 psci_migrate_info_type(void);

#define psci_32_migrate_info_up_cpu_func_id 0x84000007
i32 psci_migrate_info_up_cpu_32(void);
#define psci_64_migrate_info_up_cpu_func_id 0xC4000007
i64 psci_migrate_info_up_cpu_64(void);

#define psci_system_off_func_id 0x84000008
void psci_system_off(void);

#define psci_32_system_off_2_func_id 0x84000015
i32 psci_system_off_2_32(u32 type, u32 cookie);
#define psci_64_system_off_2_func_id 0xC4000015
i64 psci_system_off_2_64(u32 type, u64 cookie);

#define psci_system_reset_func_id 0x84000009
void psci_system_reset(void);

#define psci_32_system_reset_2_func_id 0x84000012
i32 psci_system_reset_2_32(u32 reset_type, u32 cookie);
#define psci_64_system_reset_2_func_id 0xC4000012
i64 psci_system_reset_2_64(u32 reset_type, u64 cookie);

#define psci_mem_protect_func_id 0x84000013
i32 psci_mem_protect(u32 enable);

#define psci_32_mem_protect_check_range_func_id 0x84000014
i32 psci_mem_protect_check_range_32(u32 base, u32 length);
#define psci_64_mem_protect_check_range_func_id 0xC4000014
i64 psci_mem_protect_check_range_64(u64 base, u64 length);

#define psci_feature_function_id 0x8400000A
i32 psci_feature(u32 psci_feature_id);

#define psci_cpu_freeze_func_id 0x8400000B
i32 psci_cpu_freeze(void);

#define psci_32_cpu_default_suspend_func_id 0x8400000C
i32 psci_cpu_default_suspend_32(u32 entry_point_address, u32 context_id);
#define psci_64_cpu_default_suspend_func_id 0xC400000C
i64 psci_cpu_default_suspend_64(u64 entry_point_address, u64 context_id);

#define psci_32_node_hw_state_func_id 0x8400000D
i32 psci_node_hw_state_32(u32 target_cpu, u32 power_level);
#define psci_64_node_hw_state_func_id 0xC400000D
i64 psci_node_hw_state_64(u64 target_cpu, u32 power_level);

#define psci_32_system_suspend_func_id 0x8400000E
i32 psci_system_suspend_32(u32 entry_point_address, u32 context_id);
#define psci_64_system_suspend_func_id 0xC400000E
i64 psci_system_suspend_64(u64 entry_point_address, u64 context_id);

#define psci_set_suspend_mode_func_id 0x8400000F
i32 psci_set_suspend_mode(u32 mode);

#define psci_32_state_residency_func_id 0x84000010
u32 psci_state_residency_32(u32 target_cpu, u32 power_state);
#define psci_64_state_residency_func_id 0xC4000010
u64 psci_state_residency_64(u64 target_cpu, u32 power_state);

#define psci_32_state_count_func_id 0x84000011
u32 psci_state_count_32(u32 target_cpu, u32 power_state);
#define psci_64_state_count_func_id 0xC4000011
u64 psci_state_count_64(u64 target_cpu, u32 power_state);
#endif