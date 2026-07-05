
.section .text
.globl _start

_start_x:
    li sp, 0x80000000
_start:
    call main
    mv a0,a0
    li a7,93
    ecall
