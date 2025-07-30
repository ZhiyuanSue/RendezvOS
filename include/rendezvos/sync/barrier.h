#ifndef _RENDEZVOS_BARRIER_H_
#define _RENDEZVOS_BARRIER_H_
#ifdef _AARCH64_
#include <arch/aarch64/sync/barrier.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/sync/barrier.h>
#elif defined _RISCV64_
#include <arch/riscv64/sync/barrier.h>
#elif defined _X86_64_
#include <arch/x86_64/sync/barrier.h>
#else
#include <arch/x86_64/sync/barrier.h>
#endif
#endif