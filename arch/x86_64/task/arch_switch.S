    .section    .text
    .global     context_switch
context_switch:
    // save the old
    mov %rsp,   (%rdi)
    mov %r15,   8(%rdi)
    mov %r14,   16(%rdi)
    mov %r13,   24(%rdi)
    mov %r12,   32(%rdi)
    mov %rbp,   40(%rdi)
    mov %rbx,   48(%rdi)

    // load the new
    mov 48(%rsi),   %rbx
    mov 40(%rsi),   %rbp
    mov 32(%rsi),   %r12
    mov 24(%rsi),   %r13
    mov 16(%rsi),   %r14
    mov 8(%rsi),    %r15
    mov (%rsi),     %rsp
    // return
    ret