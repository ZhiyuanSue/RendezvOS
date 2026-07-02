#ifndef _RENDEZVOS_KMSG_SYSTEM_H_
#define _RENDEZVOS_KMSG_SYSTEM_H_

/*
 * Rendezvos system kmsg opcodes and TLV format strings (single registry).
 *
 * Messages for system infrastructure (powerd, timer notify, …) live here.
 * hdr.module on the wire is always the destination port service_id.
 *
 * Linux compat and other upper layers define opcodes under
 * include/linux_compat/ipc/ (e.g. clean_protocol.h, vfs_protocol.h).
 */

/* Power control (powerd server port). */
#define KMSG_OP_SYSTEM_POWER_SHUTDOWN 1u
#define KMSG_OP_SYSTEM_POWER_REBOOT   2u

/* Timer one-shot notify (any rendezvos_timer_event wait_port). */
#define KMSG_OP_SYSTEM_TIMER_EXPIRE   3u
#define KMSG_OP_SYSTEM_TIMER_CANCEL   4u
#define KMSG_FMT_SYSTEM_TIMER         "q"

#endif /* _RENDEZVOS_KMSG_SYSTEM_H_ */
