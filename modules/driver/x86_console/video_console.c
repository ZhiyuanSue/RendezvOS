#include <modules/driver/x86_console/video_console.h>
#include <arch/x86_64/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/smp/percpu.h>
#include <common/string.h>

struct multiboot_framebuffer mtb1_fb_value;
//TODO: mtb2_fb_value

extern int BSP_ID;
void vbe_console_init(struct multiboot_vbe *mtb_vbe)
{
}
void fb_console_init(struct multiboot_framebuffer *mtb_fb)
{
        memcpy(&mtb1_fb_value,mtb_fb,sizeof(struct multiboot_framebuffer));
}
void fb_map_pages(){
        u32 color;
        unsigned i;
        void *fb_phy = (void *)(unsigned long)mtb1_fb_value.framebuffer_addr;
        void *fb = KERNEL_PHY_TO_VIRT(fb_phy);
        u64 fb_space_page_num = mtb1_fb_value.framebuffer_pitch*mtb1_fb_value.framebuffer_height / PAGE_SIZE;
        for (paddr phy_addr = (paddr)fb_phy, page_num = 0;
             page_num < fb_space_page_num;
             phy_addr += PAGE_SIZE, page_num++)
                map(per_cpu(current_vspace, BSP_ID),
                    PPN((paddr)(phy_addr)),
                    VPN(KERNEL_PHY_TO_VIRT((paddr)(phy_addr))),
                    3,
                    PAGE_ENTRY_DEVICE | PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                            | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                    &per_cpu(Map_Handler, BSP_ID),
                    NULL);

        switch (mtb1_fb_value.framebuffer_type) {
        case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED: {
                unsigned best_distance, distance;
                struct multiboot_color *palette;

                palette = (struct multiboot_color *)
                                  mtb1_fb_value.framebuffer_palette_addr;

                color = 0;
                best_distance = 4 * 256 * 256;

                for (i = 0; i < mtb1_fb_value.framebuffer_palette_num_colors; i++) {
                        distance = (0xff - palette[i].blue)
                                           * (0xff - palette[i].blue)
                                   + palette[i].red * palette[i].red
                                   + palette[i].green * palette[i].green;
                        if (distance < best_distance) {
                                color = i;
                                best_distance = distance;
                        }
                }
        } break;

        case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
                color = ((1 << mtb1_fb_value.framebuffer_blue_mask_size) - 1)
                        << mtb1_fb_value.framebuffer_blue_field_position;
                break;

        case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
                color = '\\' | 0x0100;
                break;

        default:
                color = 0xffffffff;
                break;
        }
        for (i = 0;
             i < mtb1_fb_value.framebuffer_width && i < mtb1_fb_value.framebuffer_height;
             i++) {
                switch (mtb1_fb_value.framebuffer_bpp) {
                case 8: {
                        u8 *pixel = fb + mtb1_fb_value.framebuffer_pitch * i + i;
                        *pixel = color;
                } break;
                case 15:
                case 16: {
                        u16 *pixel = fb + mtb1_fb_value.framebuffer_pitch * i + 2 * i;
                        *pixel = color;
                } break;
                case 24: {
                        u32 *pixel = fb + mtb1_fb_value.framebuffer_pitch * i + 3 * i;
                        *pixel = (color & 0xffffff) | (*pixel & 0xff000000);
                } break;

                case 32: {
                        u32 *pixel = fb + mtb1_fb_value.framebuffer_pitch * i + 4 * i;
                        *pixel = color;
                } break;
                }
        }
}
