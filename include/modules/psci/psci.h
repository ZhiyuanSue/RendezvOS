#ifndef _SHAMPOOS_PSCI_H_
#define _SHAMPOOS_PSCI_H_

#include <common/types.h>
#include <modules/dtb/dtb.h>
#include <modules/log/log.h>
#include "psci_error.h"
enum psci_call_method { psci_call_none = 0, psci_call_smc, psci_call_hvc };
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

#define psci_32_migrate_function_id 0x84000005
i32 psci_migrate_32(u32 target_cpu);
#define psci_64_migrate_function_id 0xC4000005
i64 psci_migrate_64(u64 target_cpu);

#define psci_migrate_info_type_function_id 0x84000006
i32 psci_migrate_info_type(void);

#define psci_32_migrate_info_up_cpu_function_id 0x84000007
void psci_migrate_info_up_cpu(void);

#define psci_system_off_function_id 0x84000008
void psci_system_off(void);
void psci_system_off_2(void);
void psci_system_reset(void);
void psci_system_reset_2(void);
void psci_mem_protect(void);
void psci_mem_protect_check_range(void);
void psci_feature(void);
void psci_freeze(void);
void psci_cpu_default_suspend(void);
void psci_node_hw_state(void);
void psci_system_suspend(void);
void psci_set_suspend_mode(void);
void psci_state_residency(void);
void psci_state_count(void);
#endif