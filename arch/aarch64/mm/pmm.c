#include <arch/aarch64/mm/pmm.h>
#include <modules/dtb/dtb.h>
#include <common/endianness.h>

extern struct property_type property_types[PROPERTY_TYPE_NUM];
static void get_dtb_memory(void* fdt,int offset,int depth){
	/*
		actually we seems to use something like of_find_node_by_type
		but now we have no memory to alloc any struct of device_node
	*/
	int property,node;
	const char *device_type_str = property_types[PROPERTY_TYPE_DEVICE_TYPE].property_string;
	const char *memory_str = "memory\0";
	const char *reg_str = property_types[PROPERTY_TYPE_REG].property_string;
	fdt_for_each_property_offset(property,fdt,offset)
	{
		struct fdt_property* prop=(struct fdt_property*)fdt_offset_ptr(fdt,property,FDT_TAGSIZE);
		const char* property_name=fdt_string(fdt,SWAP_ENDIANNESS_32(prop->nameoff));
		const char* data=(const char*)(prop->data);
		if(!strcmp(property_name,device_type_str) && !strcmp(data,memory_str)){
		 	goto find_memory_node;
		}
	}
	fdt_for_each_subnode(node,fdt,offset){
		get_dtb_memory(fdt,node,depth+1);
	}
	return;
find_memory_node:
	fdt_for_each_property_offset(property,fdt,offset)
	{
		struct fdt_property* prop=(struct fdt_property*)fdt_offset_ptr(fdt,property,FDT_TAGSIZE);
		const char *s=fdt_string(fdt,SWAP_ENDIANNESS_32(prop->nameoff));
		const char *data=(const char*)(prop->data);
		if(!strcmp(s,reg_str)){
			// find the reg node
			pr_info("find the reg\n");
		}
	}
}

void arch_init_pmm(struct setup_info* arch_setup_info)
{
	pr_info("start arch init pmm\n");
	struct fdt_header* dtb_header_ptr = (struct fdt_header*)(arch_setup_info->boot_dtb_header_base_addr);
	for(u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap);
		off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct);
		off += sizeof(struct fdt_reserve_entry) )
	{
		struct fdt_reserve_entry* entry = (struct fdt_reserve_entry*)((u64)dtb_header_ptr + off);
		pr_info("reserve_entry: address 0x%x size: 0x%x\n",entry->address,entry->size);
	}
	get_dtb_memory(dtb_header_ptr,0,0);
}
