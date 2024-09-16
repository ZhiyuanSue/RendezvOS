#ifndef _SHAMPOOS_ARCH_PAGE_TABLE_DEF_H_
#define _SHAMPOOS_ARCH_PAGE_TABLE_DEF_H_

/*we only use 4K page,and 2M and 1G block*/
#define PT_DESC_V (1)
/*descriptor level 0,1,2*/
#define PT_DESC_BLOCK_OR_TABLE (1ULL << 1)
/*table*/
#define PT_DESC_ADDR_MASK (0xfffffffff000)
/*block,no L0 block*/
#define PT_DESC_L1_BLOCK_ADDR_MASK (0xffffc0000000)
#define PT_DESC_L2_BLOCK_ADDR_MASK (0xffffffe00000)

/*descriptor level 3*/
#define PT_DESC_PAGE (1ULL << 1)

/*attribute field of table descriptor*/
#define PT_DESC_TABLE_ATTR_NSTABLE      (1ULL << 63)
#define PT_DESC_TABLE_ATTR_APTABLE_MASK (0x3ULL << 61)
#define PT_DESC_TABLE_ATTR_APTABLE_OFF  (61)
#define PT_DESC_TABLE_ATTR_UXNTABLE     (1ULL << 60)
#define PT_DESC_TABLE_ATTR_PXNTABLE     (1ULL << 59)

/*attribute field of block and page descriptor,which is divided into two
 * part:lower and upper*/
/*lower*/
#define PT_DESC_ATTR_LOWER_NG            (1ULL << 11)
#define PT_DESC_ATTR_LOWER_AF            (1ULL << 10)
#define PT_DESC_ATTR_LOWER_SH_MASK       (0x300)
#define PT_DESC_ATTR_LOWER_AP_MASK       (0xC0)
#define PT_DESC_ATTR_LOWER_NS            (1ULL << 5)
#define PT_DESC_ATTR_LOWER_ATTRINDX_MASK (0x1C)
/*upper*/
#define PT_DESC_ATTR_UPPER_XN   (1ULL << 54)
#define PT_DESC_ATTR_UPPER_PXN  (1ULL << 53)
#define PT_DESC_ATTR_UPPER_CONT (1ULL << 52)

#endif