	.section .boot.map_util
	.p2align 12
	.global MAP_L1_table
MAP_L1_table:
	.zero 8*512
	.global	MAP_L2_table
MAP_L2_table:
	.zero 8*512
	.global MAP_L3_table
MAP_L3_table:
	.zero 8*512