

PUSH_GPRS MACRO
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
ENDM

PUSH_XMM MACRO
    sub     rsp, 60h
    movaps  xmmword ptr [rsp +  0h], xmm0
    movaps  xmmword ptr [rsp + 10h], xmm1
    movaps  xmmword ptr [rsp + 20h], xmm2
    movaps  xmmword ptr [rsp + 30h], xmm3
    movaps  xmmword ptr [rsp + 40h], xmm4
    movaps  xmmword ptr [rsp + 50h], xmm5
ENDM

POP_GPRS MACRO
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
ENDM

POP_XMM MACRO
    movaps  xmm5, xmmword ptr [rsp + 50h]
    movaps  xmm4, xmmword ptr [rsp + 40h]
    movaps  xmm3, xmmword ptr [rsp + 30h]
    movaps  xmm2, xmmword ptr [rsp + 20h]
    movaps  xmm1, xmmword ptr [rsp + 10h]
    movaps  xmm0, xmmword ptr [rsp +  0h]
    add     rsp, 60h
ENDM

.CODE

EXTERN HandleHostException : PROC

Index = 0

INTERRUPT_HANDLER MACRO InterruptNumber
    db      6Ah, 000h
    db      068h
    dd      InterruptNumber
    jmp     AsmCommonExceptionHandler
Index = Index + 1
ENDM

INTERRUPT_HANDLER_WITH_CODE MACRO InterruptNumber
    db      090h, 090h
    db      068h
    dd      InterruptNumber
    jmp     AsmCommonExceptionHandler
Index = Index + 1
ENDM

AsmDefaultExceptionHandlers PROC

    REPEAT  8
    INTERRUPT_HANDLER Index
ENDM

    INTERRUPT_HANDLER_WITH_CODE Index
    INTERRUPT_HANDLER Index

    REPEAT  5
    INTERRUPT_HANDLER_WITH_CODE Index
ENDM

    REPEAT  2
    INTERRUPT_HANDLER Index
ENDM

    INTERRUPT_HANDLER_WITH_CODE Index

    REPEAT  238
    INTERRUPT_HANDLER Index
ENDM
AsmDefaultExceptionHandlers ENDP

AsmCommonExceptionHandler PROC
    PUSH_GPRS
    mov     rcx, rsp
    mov     r15, rsp
    and     rsp, 0FFFFFFFFFFFFFFF0h
    PUSH_XMM

    sub     rsp, 20h
    call    HandleHostException
    add     rsp, 20h

    POP_XMM
    mov     rsp, r15
    POP_GPRS

    add     rsp, 10h
    iretq
AsmCommonExceptionHandler ENDP

END
