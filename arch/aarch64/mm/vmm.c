#include	<arch/aarch64/mm/vmm.h>
#include	<arch/aarch64/mm/page_table_def.h>
void	inline	arch_set_L0_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags)
{
	pt_addr[(vpn<<16)>>55]=(ppn&PT_DESC_ADDR_MASK) | flags;
}
void	inline	arch_set_L1_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags)
{
	pt_addr[(vpn<<25)>>55]=(ppn&PT_DESC_ADDR_MASK) | flags;
}
void	inline	arch_set_L2_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags)
{
	pt_addr[(vpn<<34)>>55]=(ppn&PT_DESC_ADDR_MASK) | flags;
}
void	inline	arch_set_L3_entry(u64 ppn,u64 vpn,u64* pt_addr,u64 flags)
{
	pt_addr[(vpn<<43)>>55]=(ppn&PT_DESC_ADDR_MASK) | flags;
}