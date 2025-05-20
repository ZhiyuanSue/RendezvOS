#include <modules/elf/elf.h>
unsigned long elf64_hash(const unsigned char *name)
{
        unsigned long h = 0, g;
        while (*name) {
                h = (h << 4) + *name++;
                if ((g = (h & 0xf0000000)))
                        h ^= g >> 24;
                h &= 0x0fffffff;
        }
        return h;
}
bool check_elf_header(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        if (elf_ident[EI_MAG0] != ELFMAG0 || elf_ident[EI_MAG1] != ELFMAG1
            || elf_ident[EI_MAG2] != ELFMAG2 || elf_ident[EI_MAG3] != ELFMAG3)
                goto check_fail;
        if (elf_ident[EI_VERSION] != EV_CURRENT)
                goto check_fail;
        return true;
check_fail:
        return false;
}
u16 get_elf_type(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        if (elf_ident[EI_CLASS] == ELFCLASS32) {
                Elf32_Ehdr *elf_header = (Elf32_Ehdr *)elf_header_ptr;
                return elf_header->e_type;
        } else if (elf_ident[EI_CLASS] == ELFCLASS64) {
                Elf64_Ehdr *elf_header = (Elf64_Ehdr *)elf_header_ptr;
                return elf_header->e_type;
        } else {
                return ET_NONE;
        }
}
u16 get_elf_machine(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        if (elf_ident[EI_CLASS] == ELFCLASS32) {
                Elf32_Ehdr *elf_header = (Elf32_Ehdr *)elf_header_ptr;
                return elf_header->e_machine;
        } else if (elf_ident[EI_CLASS] == ELFCLASS64) {
                Elf64_Ehdr *elf_header = (Elf64_Ehdr *)elf_header_ptr;
                return elf_header->e_machine;
        } else {
                return ET_NONE;
        }
}
