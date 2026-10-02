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

/**
 * Timer one-shot notify. Payload: @c KMSG_FMT_SYSTEM_TIMER — one @c i64
 * cookie / event id.
 */
#define KMSG_OP_SYSTEM_TIMER_EXPIRE 3u
#define KMSG_OP_SYSTEM_TIMER_CANCEL 4u
#define KMSG_FMT_SYSTEM_TIMER       "q"

/**
 * Port-close wake for blocked receivers only. Blocked senders are woken with
 * @c THREAD_FLAG_IPC_PORT_CLOSED instead — see @c unregister_port /
 * @c THREAD_FLAG_IPC_PORT_CLOSED.
 */
#define KMSG_OP_SYSTEM_PORT_CLOSED  5u
/** Payload: constant 0; no semantic content. */
#define KMSG_FMT_SYSTEM_PORT_CLOSED "q"

/*
 * The upper msgs must start at the kmsg system end + 1.
 * otherwise, the code might overlap and have bug
 */
#define KMSG_OP_SYSTEM_END 6u

#endif /* _RENDEZVOS_KMSG_SYSTEM_H_ */
