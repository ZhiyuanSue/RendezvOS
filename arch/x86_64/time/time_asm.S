#define UDELAY_MAX 2000
.code64
    .section .text
    .global arch_loop_delay
arch_loop_delay:
    cmp $0,%rdi
    je arch_loop_delay_end
    sub $1,%rdi
arch_loop_delay_end:
    ret

    .global _udelay
_udelay:
    cmp $(UDELAY_MAX),%rdx
    jb _udelay_end
_udelay_loop:
    call arch_loop_delay
    sub $(UDELAY_MAX),%rdx
    cmp $(UDELAY_MAX),%rdx
    ja _udelay_loop
_udelay_end:
    ret