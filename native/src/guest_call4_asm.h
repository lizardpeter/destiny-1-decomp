#pragma once
// Shared assembly entry used by the runtime and the standalone ABI regression.
// d1_root_frame is supplied by the embedding executable.
asm(R"(
    .text
    .globl d1_call_guest4
d1_call_guest4:
    push %rbp
    push %rbx
    mov %rsp, %rbx
    and $-16, %rsp
    lea d1_root_frame(%rip), %rbp
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %rcx
    call *%rax
    mov %rbx, %rsp
    pop %rbx
    pop %rbp
    ret
)");
