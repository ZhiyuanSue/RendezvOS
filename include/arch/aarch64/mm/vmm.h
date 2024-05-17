#ifndef _SHAMPOOS_ARCH_VMM_H_
#define _SHAMPOOS_ARCH_VMM_H_
#include	<common/types.h>
void	arch_set_L0_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags);
void	arch_set_L1_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags);
void	arch_set_L2_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags);
void	arch_set_L3_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags);
#endif