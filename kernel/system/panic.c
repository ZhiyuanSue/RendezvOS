#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>

#ifdef _X86_64_
#include <arch/x86_64/power_ctrl.h>
#elif defined _AARCH64_
#include <arch/aarch64/power_ctrl.h>
#elif defined _RISCV64_
#endif

#include <rendezvos/system/panic.h>

void kernel_panic(const char* msg)
{
        pr_error("KERNEL PANIC: %s\n", msg ? msg : "(unknown)");
        pr_error("CPU %lu: System halted.\n", (u64)percpu(cpu_number));
        arch_shutdown();

        pr_error("[kernel panic] goto unreachable place,halt\n");
        while (1) {
                arch_cpu_relax();
        }
}
void kernel_halt(void)
{
        pr_error("KERNEL: System halted.\n");
        arch_shutdown();

        pr_error("[kernel halt] goto unreachable place,halt\n");
        while (1) {
                arch_cpu_relax();
        }
}
