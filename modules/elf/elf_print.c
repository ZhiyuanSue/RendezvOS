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
char elf_pt_type_str[ELF_PT_TYPE_NUM][ELF_STR_LEN] = {
        {"NULL"},
        {"LOAD"},
        {"DYNAMIC"},
        {"INTERP"},
        {"NOTE"},
        {"SHLIB"},
        {"PHDR"},
        {"TLS"},
};
void print_ph_type(u32 p_type)
{
        if (p_type <= 7) {
                debug("p_type\t\t:\t%s\n", elf_pt_type_str[p_type]);
        } else {
                switch (p_type) {
                case PT_GNU_EH_FRAME:
                        debug("p_type\t\t:\tGNU_EH_FRAME\n");
                        break;
                case PT_GNU_STACK:
                        debug("p_type\t\t:\tGNU_STACK\n");
                        break;
                case PT_GNU_RELRO:
                        debug("p_type\t\t:\tGNU_RELRO\n");
                        break;
                case PT_LOOS:
                        debug("p_type\t\t:\tLOOS\n");
                        break;
                case PT_HIOS:
                        debug("p_type\t\t:\tHIOS\n");
                        break;
                case LOPROC:
                        debug("p_type\t\t:\tLOPROC\n");
                        break;
                case HIPROC:
                        debug("p_type\t\t:\tHIPROC\n");
                        break;
                default:
                        debug("p_type\t\t:\tUNKNOWN 0x%x\n", p_type);
                        break;
                }
        }
}
void print_ph_flags(u32 p_flags)
{
        debug("p_flags\t\t:\t");
        if (p_flags & PF_X)
                debug("X");
        if (p_flags & PF_X)
                debug("W");
        if (p_flags & PF_X)
                debug("R");
        debug("\n");
}
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
        debug("=== === elf header start === ===\n");
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
        debug("=== === elf header end === ===\n");
}
void print_elf_ph32(Elf32_Phdr* phdr)
{
        debug("=== === program header start === ===\n");
        print_ph_type(phdr->p_type);
        print_ph_flags(phdr->p_flags);
        debug("p_off\t\t:\t0x%x\n", phdr->p_offset);
        debug("p_vaddr\t\t:\t0x%x\n", phdr->p_vaddr);
        debug("p_paddr\t\t:\t0x%x\n", phdr->p_paddr);
        debug("p_filesz\t:\t0x%x\n", phdr->p_filesz);
        debug("p_memsz\t\t:\t0x%x\n", phdr->p_memsz);
        debug("p_align\t\t:\t0x%x\n", phdr->p_align);
        debug("=== === program header end === ===\n");
}
void print_elf_ph64(Elf64_Phdr* phdr)
{
	pr_info("phdr addr 0x%x\n",phdr);
}
void print_elf_sh32(Elf32_Shdr* shdr)
{
}
void print_elf_sh64(Elf64_Shdr* shdr)
{
	pr_info("shdr addr 0x%x\n",shdr);
}