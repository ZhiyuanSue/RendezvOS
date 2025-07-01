#include <modules/driver/x86_console/video_console.h>

void vbe_console_init(struct multiboot_vbe *mtb_vbe)
{
}
void fb_console_init(struct multiboot_framebuffer *mtb_fb)
{
        u32 color;
        unsigned i;
        void *fb = (void *)(unsigned long)mtb_fb->framebuffer_addr;

        switch (mtb_fb->framebuffer_type) {
        case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED: {
                unsigned best_distance, distance;
                struct multiboot_color *palette;

                palette = (struct multiboot_color *)
                                  mtb_fb->framebuffer_palette_addr;

                color = 0;
                best_distance = 4 * 256 * 256;

                for (i = 0; i < mtb_fb->framebuffer_palette_num_colors; i++) {
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
                color = ((1 << mtb_fb->framebuffer_blue_mask_size) - 1)
                        << mtb_fb->framebuffer_blue_field_position;
                break;

        case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
                color = '\\' | 0x0100;
                break;

        default:
                color = 0xffffffff;
                break;
        }
        for (i = 0;
             i < mtb_fb->framebuffer_width && i < mtb_fb->framebuffer_height;
             i++) {
                switch (mtb_fb->framebuffer_bpp) {
                case 8: {
                        u8 *pixel = fb + mtb_fb->framebuffer_pitch * i + i;
                        *pixel = color;
                } break;
                case 15:
                case 16: {
                        u16 *pixel = fb + mtb_fb->framebuffer_pitch * i + 2 * i;
                        *pixel = color;
                } break;
                case 24: {
                        u32 *pixel = fb + mtb_fb->framebuffer_pitch * i + 3 * i;
                        *pixel = (color & 0xffffff) | (*pixel & 0xff000000);
                } break;

                case 32: {
                        u32 *pixel = fb + mtb_fb->framebuffer_pitch * i + 4 * i;
                        *pixel = color;
                } break;
                }
        }
}
