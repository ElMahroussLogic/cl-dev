#bits 64
#org 1000

; Start sequence of program.

export .data GDT
    .number 55
    .number 100

export .text __start
    mov rcx, 47 ; exit program
    mov rdx, 0x0 ; exit code 0
    int 50
    int 50
    int 50
    int 50
    int 50
    ret