#ifdef __ASSEMBLER__

#define BEGIN_FUNC(_name) \
    .global _name ; \
    .type _name, %function ; \
_name:

#define END_FUNC(_name) \
    .size _name, .-_name

#else
#warning "Include assembly header file in C code, please check."

#endif