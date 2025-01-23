#include <common/endianness.h>
#include <common/string.h>
#include <modules/dtb/dtb.h>
#include <modules/dtb/print_property.h>

// As for this file, although the print function for dtb is useless
// but it logic is the same as that in dtb parse, so it can be reused and used
// for check
extern struct property_type property_types[255];
void print_property_value_empty(enum property_type_enum p_type, void *data,
                                uint32_t len)
{
        return;
}
void print_property_value_u32(enum property_type_enum p_type, void *data,
                              uint32_t len)
{
        u32 *u32_data;

        u32_data = (u32 *)data;
        print_property("<");
        for (int index = 0; index < len; index += sizeof(u32)) {
                print_property("0x%x", SWAP_ENDIANNESS_32(*u32_data));
                if (index + sizeof(u32) >= len) {
                        print_property(">");
                } else {
                        print_property(" ");
                }
                u32_data++;
        }
}
void print_property_value_u64(enum property_type_enum p_type, void *data,
                              uint32_t len)
{
        u64 *u64_data;

        u64_data = (u64 *)data;
        print_property("<");
        for (int index = 0; index < len; index += sizeof(u64)) {
                print_property("0x%x", SWAP_ENDIANNESS_64(*u64_data));
                if (index + sizeof(u64) >= len) {
                        print_property(">");
                } else {
                        print_property(" ");
                }
                u64_data++;
        }
}
void print_property_value_string(enum property_type_enum p_type, void *data,
                                 uint32_t len)
{
        print_property("%s\n", data);
}
void print_property_value_prop_encoded_array(enum property_type_enum p_type,
                                             void *data, uint32_t len)
{
        u32 *u32_data;

        switch (p_type) {
        case PROPERTY_TYPE_REG: {
                u32_data = (u32 *)data;
                for (int index = 0; index < len; index += sizeof(u32) * 2) {
                        print_property("<");
                        print_property("addr:0x%x ",
                                       SWAP_ENDIANNESS_32(*u32_data));
                        u32_data++;
                        print_property("len:0x%x>",
                                       SWAP_ENDIANNESS_32(*u32_data));
                        u32_data++;
                        if (index + sizeof(u32) * 2 < len)
                                print_property(" ");
                }
                break;
        }
        case PROPERTY_TYPE_RANGES:
                break;
        case PROPERTY_TYPE_DMA_RANGES:
                break;
        case PROPERTY_TYPE_INTERRUPT:
                break;
        case PROPERTY_TYPE_INTERRUPT_MAP:
                break;
        case PROPERTY_TYPE_INTERRUPT_MAP_MASK:
                break;
        case PROPERTY_TYPE_SPECIFIER_MAP:
                break;
        case PROPERTY_TYPE_SPECIFIER_MAP_MASK:
                break;
        case PROPERTY_TYPE_SPECIFIER_MAP_PASS_THRU:
                break;
        default:
                print_property(
                        "Error:something wrong happened in parse this dtb,please check\n");
                break;
        }
}
void print_property_value_phandle(enum property_type_enum p_type, void *data,
                                  uint32_t len)
{
}
void print_property_value_stringlist(enum property_type_enum p_type, void *data,
                                     uint32_t len)
{
        const char *ch_data = (const char *)data;
        const char *ch_data_end = ch_data + len;
        char *ch = (char *)ch_data;

        print_property("<");
        while (ch < ch_data_end) {
                print_property("%c", *ch);
                ch++;
        }
        print_property(">");
}

void (*print_property_value_list[7])(enum property_type_enum p_type, void *data,
                                     uint32_t len) = {
        print_property_value_empty,
        print_property_value_u32,
        print_property_value_u64,
        print_property_value_string,
        print_property_value_prop_encoded_array,
        print_property_value_phandle,
        print_property_value_stringlist};

void print_property_value(const char *property_name, void *data, uint32_t len)
{
        enum property_type_enum p_type;
        enum property_value_type p_value;

        p_type = get_property_type(property_name);
        p_value = property_types[p_type].value_enum[0];
        print_property_value_list[p_value](
                p_type, data, SWAP_ENDIANNESS_32(len));
}
void parse_print_dtb(void *fdt, int offset, int depth)
{
        char *ch;
        struct fdt_property *prop;
        const char *property_name;

        /*This function is just an example of how to parse the dtb*/
        int property, node;
        ch = (char *)fdt + fdt_off_dt_struct(fdt) + offset + FDT_TAGSIZE;
        rep_print(depth, '\t', print_property);
        print_property("%s{\n", ch);
        fdt_for_each_property_offset(property, fdt, offset)
        {
                prop = (struct fdt_property *)fdt_offset_ptr(
                        fdt, property, FDT_TAGSIZE);
                rep_print(depth + 1, '\t', print_property);
                property_name =
                        fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
                print_property("%s\t:\t", property_name);
                print_property_value(property_name, prop->data, prop->len);
                print_property("\n");
        }
        fdt_for_each_subnode(node, fdt, offset)
        {
                parse_print_dtb(fdt, node, depth + 1);
        }
        rep_print(depth, '\t', pr_info);
        pr_info("}\n");
}
