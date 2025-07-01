#ifndef _X86_VIDEO_CONSOLE_
#define _X86_VIDEO_CONSOLE_
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/multiboot2.h>

void fb_console_init(struct multiboot_framebuffer* mtb_fb);

#ifdef _X86_64_

#else

#endif

#endif