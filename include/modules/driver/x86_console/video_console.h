#ifndef _X86_VIDEO_CONSOLE_
#define _X86_VIDEO_CONSOLE_
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/multiboot2.h>
void fb1_map_pages();
void fb2_map_pages();

void mtb1_fb_console_init(struct multiboot_framebuffer *mtb_fb);

void mtb2_fb_console_init(struct multiboot_tag_framebuffer *mtb_fb);
void fb1_show();
void fb2_show();
#ifdef _X86_64_

#else

#endif

#endif