#include <rendezvos/task/id.h>
Id_Manager pid_manager;
DEFINE_PER_CPU(struct spin_lock_t, pid_spin_lock);

Id_Manager tid_manager;
DEFINE_PER_CPU(struct spin_lock_t, tid_spin_lock);
void init_id_manager(Id_Manager* im)
{
        if (!im) {
                return;
        }
        im->id = 0;
        im->spin_ptr = NULL;
}
i64 get_new_pid() /*we think the vspace id is the same as the pid*/
{
        lock_mcs(&pid_manager.spin_ptr, &percpu(pid_spin_lock));
        i64 pid = pid_manager.id++;
        unlock_mcs(&pid_manager.spin_ptr, &percpu(pid_spin_lock));
        return pid;
}
i64 get_new_tid(void)
{
        lock_mcs(&tid_manager.spin_ptr, &percpu(tid_spin_lock));
        i64 tid = tid_manager.id++;
        unlock_mcs(&tid_manager.spin_ptr, &percpu(tid_spin_lock));
        return tid;
}