#ifndef _SHAMPOOS_CPU_TOPOLOGY_H_
#define _SHAMPOOS_CPU_TOPOLOGY_H_

struct cpu_topology_root {
        struct cpu_socket* first_socket;
};
struct cpu_socket {
        struct cpu_socket* sibling;
        struct cpu_cluster* first_cluster;
};
struct cpu_cluster {
        struct cpu_socket* socket;
        struct cpu_cluster* sibling;
        struct cpu_core* first_core;
};
struct cpu_core {
        int cpu_id;
        struct cpu_core* sibling;
        struct cpuinfo* cpu_info;
};

#endif