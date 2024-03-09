#include <shampoos/types.h>

struct desc_selector{
	u16 rpl:2;
	u16 table_indicator:1;
	u16 index:13;
};

/* segment descriptors */
struct segdesc {
	u32 limit_15_0 : 16;			// low bits of segment limit
	u32 base_address_15_0 : 16;		// low bits of segment base address
	u32 base_address_23_16 : 8;		// middle bits of segment base address
	u32 type : 4;					// segment type (see STS_ constants)
	u32 s : 1;						// 0 = system, 1 = application
	u32 dpl : 2;					// descriptor Privilege Level
	u32 p : 1;						// present
	u32 limit_19_16 : 4;			// high bits of segment limit
	u32 avl : 1;					// unused (available for software use)
	u32 l : 1;						// 64-bit code segment
	u32 d_or_b : 1;					// 0 = 16-bit segment, 1 = 32-bit segment
	u32 g : 1;						// granularity: limit scaled by 4K when set
	u32 base_address_31_24 : 8;		// high bits of segment base address
};
