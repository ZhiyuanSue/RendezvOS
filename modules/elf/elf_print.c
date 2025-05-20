#include <modules/elf/elf.h>
#include <modules/elf/elf_print.h>
#define debug pr_debug

char elf_e_ident_class_str[ELF_E_IDENT_CLASS_NUM][ELF_STR_LEN] = {
        {"No class"},
        {"Elf 32 file"},
        {"Elf 64 file"},
};

char elf_e_ident_data_str[ELF_E_IDENT_DATA_NUM][ELF_STR_LEN] = {
        {"No data"},
        {"LSB data"},
        {"MSB data"},
};
void print_elf_type(u16 elf_type)
{
        switch (elf_type) {
        case ET_NONE:
                debug("type\t\t:\tNo file\n");
                break;
        case ET_REL:
                debug("type\t\t:\tRelocatable file\n");
                break;
        case ET_EXEC:
                debug("type\t\t:\tExecutable file\n");
                break;
        case ET_DYN:
                debug("type\t\t:\tShared file\n");
                break;
        case ET_CORE:
                debug("type\t\t:\tCore file\n");
                break;
        case ET_LOOS:
                debug("type\t\t:\tET_LOOS\n");
                break;
        case ET_HIOS:
                debug("type\t\t:\tET_HIOS\n");
                break;
        case ET_LOPROC:
                debug("type\t\t:\tET_LOPROC\n");
                break;
        case ET_HIPROC:
                debug("type\t\t:\tET_HIPROC\n");
                break;
        default:
                debug("type\t\t:\tUNKNOWN\n");
                break;
        }
}
void print_elf_machine(u16 elf_machine_type)
{
        switch (elf_machine_type) {
        case EM_X86_64:
                debug("machine\t\t:\tX86_64\n");
                break;
        case EM_AARCH64:
                debug("machine\t\t:\tAARCH64\n");
                break;
        case EM_RISCV:
                debug("machine\t\t:\tRISCV\n");
                break;
        case EM_LOONGARCH:
                debug("machine\t\t:\tLOONGARCH\n");
                break;
        default:
                debug("machine\t\t:\tUNKNOWN\n");
                break;
        }
}
/*please use elf check header function check it before print*/
void print_elf_header(vaddr elf_header_ptr)
{
        debug("e_ident\t\t:\t");
        unsigned char* e_ident_ptr = (unsigned char*)elf_header_ptr;
        for (int i = 0; i < EI_NIDENT; i++) {
                debug("0x%x ", e_ident_ptr[i]);
        }
        debug("\n");
        debug("e_class\t\t:\t%s\n",
              elf_e_ident_class_str[e_ident_ptr[EI_CLASS]]);
        debug("e_data\t\t:\t%s\n", elf_e_ident_data_str[e_ident_ptr[EI_DATA]]);
        print_elf_type(get_elf_type(elf_header_ptr));
        if (e_ident_ptr[EI_VERSION] == EV_CURRENT) {
                debug("version\t\t:\t1(Current)\n");
        } else {
                debug("version\t\t:\t%d(unknown,error)\n",
                      e_ident_ptr[EI_VERSION]);
                return;
        }
        print_elf_machine(get_elf_machine(elf_header_ptr));
        if (get_elf_class(elf_header_ptr) == ELFCLASS32) {
                Elf32_Ehdr* header = ELF32_HEADER(elf_header_ptr);
                debug("entry\t\t:\t0x%x\n", header->e_entry);
                debug("ehsize\t\t:\t0x%x\n", header->e_ehsize);
                debug("flags\t\t:\t0x%x\n", header->e_flags);
                debug("shstrndx\t:\t0x%x\n", header->e_shstrndx);

                debug("phoff\t\t:\t0x%x\n", header->e_phoff);
                debug("phentsize\t:\t0x%x\n", header->e_phentsize);
                debug("phnum\t\t:\t0x%x\n", header->e_phnum);

                debug("shoff\t\t:\t0x%x\n", header->e_shoff);
                debug("shentsize\t:\t0x%x\n", header->e_shentsize);
                debug("shnum\t\t:\t0x%x\n", header->e_shnum);
        } else if (get_elf_class(elf_header_ptr) == ELFCLASS64) {
                Elf64_Ehdr* header = ELF64_HEADER(elf_header_ptr);
                debug("entry\t\t:\t0x%x\n", header->e_entry);
                debug("ehsize\t\t:\t0x%x\n", header->e_ehsize);
                debug("flags\t\t:\t0x%x\n", header->e_flags);
                debug("shstrndx\t:\t0x%x\n", header->e_shstrndx);

                debug("phoff\t\t:\t0x%x\n", header->e_phoff);
                debug("phentsize\t:\t0x%x\n", header->e_phentsize);
                debug("phnum\t\t:\t0x%x\n", header->e_phnum);

                debug("shoff\t\t:\t0x%x\n", header->e_shoff);
                debug("shentsize\t:\t0x%x\n", header->e_shentsize);
                debug("shnum\t\t:\t0x%x\n", header->e_shnum);
        } else {
                return;
        }
}
void print_elf_ph32(Elf32_Phdr* phdr)
{
}
void print_elf_ph64(Elf64_Phdr* phdr)
{
}
void print_elf_sh32(Elf32_Shdr* phdr)
{
}
void print_elf_sh64(Elf64_Shdr* phdr)
{
}