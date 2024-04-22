#ifndef _SHAMPOOS_ARCH_PAGE_TABLE_H_
#define _SHAMPOOS_ARCH_PAGE_TABLE_H_
#include <shampoos/types.h>
typedef u64 pt_entry;
#define	mask_9_bit	0x1ff
#define	PML4(addr)	((addr>>39) & mask_9_bit)
#define PDPT(addr)	((addr>>30) & mask_9_bit)
#define PDT(addr)	((addr>>21) & mask_9_bit)
#define	PT(addr)	((addr>>12) & mask_9_bit)

/*mask of MAXPHYADDR*/
#define	MAXPHYADDR_mask(m)	((1<<m)-1)

/*some bit of cr3*/
#define	CR3_PWT	(1<<3)
#define CR3_PCD	(1<<4)
#define	CR3_ADDR(addr,m)	(((addr>>12)<<12) & MAXPHYADDR_mask(m))
#define	CR3_PCID(pcid)	(pcid & ((1<<12)-1))

/*some bit of PML4E*/
#define	PML4E_P	(1<<0)
#define	PML4E_RW	(1<<1)
#define	PML4E_US	(1<<2)
#define	PML4E_PWT	(1<<3)
#define	PML4E_PCD	(1<<4)
#define	PML4E_A	(1<<5)
#define	PML4E_PS	(1<<7)
#define	PML4E_ADDR(addr,m)	(((addr<<12)>>12) & MAXPHYADDR_mask(m))
#define	PML4E_XD	(1<<63)

/*some bit of PDPT*/
#define	PDPTE_P	(1<<0)
#define	PDPTE_RW	(1<<1)
#define	PDPTE_US	(1<<2)
#define	PDPTE_PWT	(1<<3)
#define	PDPTE_PCD	(1<<4)
#define	PDPTE_A	(1<<5)
#define	PDPTE_PS	(1<<7)
#define	PDPTE_G	(1<<8)	/*1G huge page only*/
#define	PDPTE_ADDR(addr,m)	(((addr>>12)<<12) & MAXPHYADDR_mask(m))
#define	PDPTE_PAT	(1<<12)	/*1G huge page only*/
#define	PDPTE_ADDR_1G(addr,m)	(((addr>>30)<<30) &MAXPHYADDR_mask(m))	/*1G huge page only*/
#define	PDPTE_PKE(prot_key)	((prot_key & 0xf)<<59)	/*1G huge page only*/
#define	PDPTE_XD	(1<<63)

/*some bit of PD*/
#define	PDE_P	(1<<0)
#define	PDE_RW	(1<<1)
#define	PDE_US	(1<<2)
#define	PDE_PWT	(1<<3)
#define	PDE_PCD	(1<<4)
#define	PDE_A	(1<<5)
#define	PDE_PS	(1<<7)
#define	PDE_G	(1<<8)	/*2m huge page only*/
#define	PDE_ADDR(addr,m)	(((addr>>12)<<12) & MAXPHYADDR_mask(m))
#define	PDE_PAT	(1<<12)	/*2m huge page only*/
#define	PDE_ADDR_2M(addr,m)	(((addr>>21)<<21) & MAXPHYADDR_mask(m))	/*2m huge page only*/
#define	PDE_PKE(prot_key)	((prot_key & 0xf)<<59)	/*2m huge page only*/
#define	PDE_XD	(1<<63)


/*some bit of PT*/
#define	PTE_P	(1<<0)
#define	PTE_RW	(1<<1)
#define	PTE_US	(1<<2)
#define	PTE_PWT	(1<<3)
#define	PTE_PCD	(1<<4)
#define	PTE_A	(1<<5)
#define PTE_D	(1<<6)
#define	PTE_PAT	(1<<7)
#define	PTE_G	(1<<8)
#define	PTE_ADDR(addr,m)	(((addr<<12)>>12) & MAXPHYADDR_mask(m))
#define	PDE_PKE(prot_key)	((prot_key & 0xf)<<59)
#define	PTE_XD	(1<<63)

#endif