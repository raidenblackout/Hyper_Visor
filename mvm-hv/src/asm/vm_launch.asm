

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

EXTERN HandleVmExit : PROC

AsmLaunchVm PROC
    mov     rsp, rcx

AsLV10:
    mov     rax, [rsp]
    vmload  rax

    vmrun   rax

    vmsave  rax

    PUSH_GPRS
    mov     rcx, rsp
    mov     r15, rsp
    and     rsp, 0FFFFFFFFFFFFFFF0h
    PUSH_XMM

    sub     rsp, 20h
    call    HandleVmExit
    add     rsp, 20h

    POP_XMM
    mov     rsp, r15
    POP_GPRS
    jmp     AsLV10
AsmLaunchVm ENDP

AsmReadInstructionPointer PROC
    mov     rax, [rsp]
    ret
AsmReadInstructionPointer ENDP

AsmReadStackPointer PROC
    mov     rax, rsp
    add     rax, 8
    ret
AsmReadStackPointer ENDP

AsmVmRunMicroVm PROC

    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    push    rcx
    push    rdx

    mov     rax, rdx

    push    r8
    sub     rsp, 200h
    fxsave64 [rsp]
    fxrstor64 [r8]

    mov     rbx, [rax +  0*8]
    mov     rcx, [rax +  1*8]
    mov     rdx, [rax +  2*8]
    mov     rsi, [rax +  3*8]
    mov     rdi, [rax +  4*8]
    mov     rbp, [rax +  5*8]
    mov     r8,  [rax +  6*8]
    mov     r9,  [rax +  7*8]
    mov     r10, [rax +  8*8]
    mov     r11, [rax +  9*8]
    mov     r12, [rax + 10*8]
    mov     r13, [rax + 11*8]
    mov     r14, [rax + 12*8]
    mov     r15, [rax + 13*8]

    mov     rax, [rsp + 210h]

    vmload  rax
    vmrun   rax
    vmsave  rax

    push    rax
    mov     rax, [rsp + 208h]
    fxsave64 [rax]
    fxrstor64 [rsp + 8]
    pop     rax
    add     rsp, 208h

    push    rax
    mov     rax, [rsp + 8]
    mov     [rax +  0*8], rbx
    mov     [rax +  1*8], rcx
    mov     [rax +  2*8], rdx
    mov     [rax +  3*8], rsi
    mov     [rax +  4*8], rdi
    mov     [rax +  5*8], rbp
    mov     [rax +  6*8], r8
    mov     [rax +  7*8], r9
    mov     [rax +  8*8], r10
    mov     [rax +  9*8], r11
    mov     [rax + 10*8], r12
    mov     [rax + 11*8], r13
    mov     [rax + 12*8], r14
    mov     [rax + 13*8], r15

    pop     rax
    mov     rax, [rax + 070h]

    add     rsp, 16

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    ret
AsmVmRunMicroVm ENDP

END
