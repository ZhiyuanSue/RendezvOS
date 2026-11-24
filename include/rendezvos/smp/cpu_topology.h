#ifndef _RENDEZVOS_CPU_TOPOLOGY_H_
#define _RENDEZVOS_CPU_TOPOLOGY_H_

#include <common/limits.h>
#include <common/types.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/cpu_id.h>

/* —— Logical ↔ hardware map —— */

/**
 * @brief Hardware CPU identity (wake / IPI destination).
 *
 * x86_64: Local APIC id. aarch64: MPIDR affinity.
 * Filled at enumeration; never reshuffled on bring-up success or failure.
 */
typedef u64 cpu_hwid_t;

#define CPU_HWID_INVALID ((cpu_hwid_t)U64_MAX)

extern cpu_hwid_t cpu_hw_map[RENDEZVOS_MAX_CPU_NUMBER];

static inline void cpu_hw_map_reset(void)
{
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++)
                cpu_hw_map[i] = CPU_HWID_INVALID;
}

static inline cpu_hwid_t cpu_hw_id(cpu_id_t cpu)
{
        if (cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return CPU_HWID_INVALID;
        return cpu_hw_map[cpu];
}

static inline void set_cpu_hw_id(cpu_id_t cpu, cpu_hwid_t hwid)
{
        if (cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                cpu_hw_map[cpu] = hwid;
}

/* —— Hierarchy placeholders (not walked in v0.1) —— */

struct cpu_topology_root {
        struct cpu_socket *first_socket;
};
struct cpu_socket {
        struct cpu_socket *sibling;
        struct cpu_cluster *first_cluster;
};
struct cpu_cluster {
        struct cpu_socket *socket;
        struct cpu_cluster *sibling;
        struct cpu_core *first_core;
};
/** Leaf: runnable CPU is identified by logical @c cpu_id. */
struct cpu_core {
        cpu_id_t cpu_id;
        struct cpu_core *sibling;
        struct cpuinfo *cpu_info;
};

#endif
