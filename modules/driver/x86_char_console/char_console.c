#include <modules/driver/x86_char_console/char_console.h>
#include <common/string.h>

#define FORWORD_COLOR_MATRIX_OFFSET  30
#define BACKWORD_COLOR_MATRIX_OFFSET 40
u8 forword_color_matrix[10] = {X86_CHAR_CONSOLE_FORWORD_BLACK,
                               X86_CHAR_CONSOLE_FORWORD_RED,
                               X86_CHAR_CONSOLE_FORWORD_GREEN,
                               X86_CHAR_CONSOLE_FORWORD_YELLOW,
                               X86_CHAR_CONSOLE_FORWORD_BLUE,
                               X86_CHAR_CONSOLE_FORWORD_NONE, /*unrealized*/
                               X86_CHAR_CONSOLE_FORWORD_NONE,
                               X86_CHAR_CONSOLE_FORWORD_NONE,
                               X86_CHAR_CONSOLE_FORWORD_NONE,
                               X86_CHAR_CONSOLE_FORWORD_NONE};
u8 backword_color_matrix[10] = {X86_CHAR_CONSOLE_FORWORD_NONE};

void set_console(struct x86_char_console* console, u64 xlimit, u64 ylimit,
                 u64 color)
{
        console->console_vaddr_base =
                KERNEL_VIRT_OFFSET + CHAR_CONSOLE_PHY_BASE;
        console->xpos_size = xlimit;
        console->ypos_size = ylimit;
        console->xpos_curr = 0;
        console->ypos_curr = 0;
        console->color = color;
}
void cls(struct x86_char_console* console)
{
        memset((void*)(console->console_vaddr_base),
               0,
               console->xpos_size * console->ypos_size * 2);
        console->xpos_curr = 0;
        console->ypos_curr = 0;
}
void char_console_putc(struct x86_char_console* console, char c)
{
        if (c == '\n' || c == '\r') {
        newline:
                console->xpos_curr = 0;
                console->ypos_curr++;
                if (console->ypos_curr >= console->ypos_size)
                        console->ypos_curr = 0;
                return;
        }

        *((u8*)(console->console_vaddr_base
                + (console->xpos_curr + console->ypos_curr * console->ypos_size)
                          * 2)) = c & 0xFF;
        *((u8*)(console->console_vaddr_base
                + (console->xpos_curr + console->ypos_curr * console->ypos_size)
                          * 2
                + 1)) = (u8)(console->color & 0xFF);

        console->xpos_curr++;
        if (console->xpos_curr >= console->xpos_size)
                goto newline;
}
u8 map_color(u64 forword_color, u64 backword_color)
{
        u8 char_color = forword_color_matrix[forword_color
                                             - FORWORD_COLOR_MATRIX_OFFSET];
        u8 bg_color = backword_color_matrix[backword_color
                                            - BACKWORD_COLOR_MATRIX_OFFSET];
        return bg_color << 4 + char_color;
}