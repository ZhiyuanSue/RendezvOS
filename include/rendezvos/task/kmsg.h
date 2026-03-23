#ifndef _RENDEZVOS_KMSG_H_
#define _RENDEZVOS_KMSG_H_

#include <common/types.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/task/id.h>
#include <rendezvos/task/message.h>

#define RENDEZ_MSG_TYPE_KERNEL 1

#define KMSG_MAGIC   0x4b4d5347u
#define KMSG_VERSION 1u

struct Thread_Base;

enum kmsg_kind {
        KMSG_KIND_NONE = 0,
        KMSG_KIND_THREAD_EXIT = 1,
};

typedef struct {
        u32 magic;
        u16 version;
        u16 kind;
        u32 payload_len;
        cpu_id_t src_cpu;
        cpu_id_t reserved_cpu;
        tid_t src_tid;
        pid_t src_pid;
} kmsg_hdr_t;

typedef struct {
        kmsg_hdr_t hdr;
        u8 payload[];
} kmsg_t;

typedef struct {
        struct Thread_Base* thread;
        i64 exit_code;
} kmsg_thread_exit_t;

Msg_Data_t* kmsg_create(u16 kind, const void* payload, u32 payload_len);
const kmsg_t* kmsg_from_msg(const Message_t* msg);

#endif
