#ifndef _SHAMPOOS_ARCH_PAGE_TABLE_DEF_H_
#define _SHAMPOOS_ARCH_PAGE_TABLE_DEF_H_

/*mask of MAXPHYADDR*/
#define MAXPHYADDR_mask(m) (((1ULL) << m) - 1)

/*some bit of PML4E*/
#define PML4E_P             (1ULL << 0)
#define PML4E_RW            (1ULL << 1)
#define PML4E_US            (1ULL << 2)
#define PML4E_PWT           (1ULL << 3)
#define PML4E_PCD           (1ULL << 4)
#define PML4E_A             (1ULL << 5)
#define PML4E_PS            (1ULL << 7)
#define PML4E_ADDR(addr, m) (((addr >> 12) << 12) & MAXPHYADDR_mask(m))
#define PML4E_XD            (1ULL << 63)

/*some bit of PDPT*/
#define PDPTE_P             (1ULL << 0)
#define PDPTE_RW            (1ULL << 1)
#define PDPTE_US            (1ULL << 2)
#define PDPTE_PWT           (1ULL << 3)
#define PDPTE_PCD           (1ULL << 4)
#define PDPTE_A             (1ULL << 5)
#define PDPTE_D             (1ULL << 6)
#define PDPTE_PS            (1ULL << 7)
#define PDPTE_G             (1ULL << 8) /*1G huge page only*/
#define PDPTE_ADDR(addr, m) (((addr >> 12) << 12) & MAXPHYADDR_mask(m))
#define PDPTE_PAT           (1ULL << 12)
/*1G huge page only*/
#define PDPTE_ADDR_1G(addr, m) (((addr >> 30) << 30) & MAXPHYADDR_mask(m))
/*1G huge page only*/
#define PDPTE_PKE(prot_key) ((prot_key & 0xf) << 59)
/*1G huge page only*/
#define PDPTE_XD (1ULL << 63)

/*some bit of PD*/
#define PDE_P             (1ULL << 0)
#define PDE_RW            (1ULL << 1)
#define PDE_US            (1ULL << 2)
#define PDE_PWT           (1ULL << 3)
#define PDE_PCD           (1ULL << 4)
#define PDE_A             (1ULL << 5)
#define PDE_D             (1ULL << 6)
#define PDE_PS            (1ULL << 7)
#define PDE_G             (1ULL << 8) /*2m huge page only*/
#define PDE_ADDR(addr, m) (((addr >> 12) << 12) & MAXPHYADDR_mask(m))
#define PDE_PAT           (1ULL << 12)
/*2m huge page only*/
#define PDE_ADDR_2M(addr, m) (((addr >> 21) << 21) & MAXPHYADDR_mask(m))
/*2m huge page only*/
#define PDE_PKE(prot_key) ((prot_key & 0xf) << 59)
/*2m huge page only*/
#define PDE_XD (1ULL << 63)

/*some bit of PT*/
#define PTE_P             (1ULL << 0)
#define PTE_RW            (1ULL << 1)
#define PTE_US            (1ULL << 2)
#define PTE_PWT           (1ULL << 3)
#define PTE_PCD           (1ULL << 4)
#define PTE_A             (1ULL << 5)
#define PTE_D             (1ULL << 6)
#define PTE_PAT           (1ULL << 7)
#define PTE_G             (1ULL << 8)
#define PTE_ADDR(addr, m) (((addr << 12) >> 12) & MAXPHYADDR_mask(m))
#define PDE_PKE(prot_key) ((prot_key & 0xf) << 59)
#define PTE_XD            (1ULL << 63)

#endif