#ifndef _X86_CHAR_CONSOLE_
#define _X86_CHAR_CONSOLE_

#define KERNEL_VIRT_OFFSET    0xffff800000000000
#define CHAR_CONSOLE_PHY_BASE 0xB8000

#include <common/types.h>
#include <common/align.h>
#include <common/string.h>
struct x86_char_console {
        vaddr console_vaddr_base;
        u64 xpos_size;
        u64 ypos_size;
        u64 xpos_curr;
        u64 ypos_curr;
        u64 color;
};
extern struct x86_char_console X86_CHAR_CONSOLE;
void set_console_color(struct x86_char_console* console, u64 color);
void set_console_size(struct x86_char_console* console, u64 xlimit, u64 ylimit);
void clear_screen(struct x86_char_console* console);
void clear_line(struct x86_char_console* console, u64 line);
/**
 * @brief Write one character to VGA text at @c CHAR_CONSOLE_PHY_BASE
 *        (+ @c KERNEL_VIRT_OFFSET ). Handles \\n/\\r, tab, wrap, scroll.
 */
void char_console_putc(struct x86_char_console* console, char c);

#define X86_CHAR_CONSOLE_FORWORD_NONE   0
#define X86_CHAR_CONSOLE_FORWORD_BLACK  0
#define X86_CHAR_CONSOLE_FORWORD_BLUE   1
#define X86_CHAR_CONSOLE_FORWORD_GREEN  2
#define X86_CHAR_CONSOLE_FORWORD_RED    4
#define X86_CHAR_CONSOLE_FORWORD_YELLOW 6

/**
 * @brief Map ANSI-ish fg/bg codes (30+/40+) to VGA attribute nibble pair.
 */
u8 map_color(u64 forward_color, u64 backword_color);
#ifdef _X86_64_

#define SET_CONSOLE_COLOR(console, color) set_console_color(console, color)
#define CONSOLE_PUTC(console, c)          char_console_putc(console, c)
#define CONSOLE_CLEAN_SCREEN(console)     clear_screen(console)

#else
#define SET_CONSOLE_COLOR(console, color)
#define CONSOLE_PUTC(console, c)
#define CONSOLE_CLEAN_SCREEN(console)
#endif

#endif