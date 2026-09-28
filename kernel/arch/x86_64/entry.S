/* entry.S - Einsprung aus Limine: eigener Stapel, dann kmain. */

    .text
    .globl _start
_start:
    leaq boot_stack_top(%rip), %rsp
    xorl %ebp, %ebp
    call kmain
1:  cli
    hlt
    jmp 1b

    .bss
    .balign 16
boot_stack:
    .skip 8192
boot_stack_top:
