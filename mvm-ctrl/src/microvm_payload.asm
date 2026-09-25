.CODE

PUBLIC microvm_payload
PUBLIC microvm_payload_end

microvm_payload PROC

    mov     rax, 05A5A5A5A5A5A5A5h
    mov     qword ptr [rsp - 16h], rax
    mov     rax, 0A5A5A5A5A5A5A5A5h
    mov     qword ptr [rsp - 08h], rax

    mov     rax, 0DEADBEEFCAFEBABEh
    mov     qword ptr [rsp - 20h], rax

    mov     rax, 0D0EEF00DDEADCAFEh
    vmmcall

    ret
microvm_payload_end LABEL BYTE
microvm_payload ENDP

PUBLIC microvm_decryption_stub
PUBLIC microvm_decryption_stub_end

microvm_decryption_stub PROC

    lea     rsi, payload_start

    mov     ecx, 0DEADBEEFh
    mov     dl, 5Ah
decrypt_loop:
    xor     byte ptr [rsi], dl
    inc     rsi
    loop    decrypt_loop
payload_start:

microvm_decryption_stub_end LABEL BYTE
microvm_decryption_stub ENDP

PUBLIC microvm_check_yield, microvm_check_yield_end
PUBLIC microvm_check_fault, microvm_check_fault_end
PUBLIC microvm_check_done, microvm_check_done_end
PUBLIC microvm_check_spin, microvm_check_spin_end

microvm_check_yield LABEL BYTE
    pcmpeqd xmm6, xmm6
    mov eax, 5
    movd xmm7, eax
    mov r12d, 3
check_yield_again:
    mov rax, 0D0EEF00DDEAD0002h
    vmmcall
    paddq xmm7, xmm7
    dec r12d
    jnz check_yield_again
    pmovmskb eax, xmm6
    cmp eax, 0FFFFh
    jne check_yield_failed
    movq rdx, xmm7
    cmp rdx, 40
    jne check_yield_failed
    mov rax, 0D0EEF00DDEADCAFEh
    vmmcall
check_yield_failed:
    xor eax, eax
    vmmcall
    ud2
microvm_check_yield_end LABEL BYTE

microvm_check_fault LABEL BYTE
    ud2
microvm_check_fault_end LABEL BYTE

microvm_check_done LABEL BYTE
    mov rax, 0D0EEF00DDEADCAFEh
    vmmcall
    ud2
microvm_check_done_end LABEL BYTE

microvm_check_spin LABEL BYTE
spin_forever:
    pause
    jmp     spin_forever
microvm_check_spin_end LABEL BYTE

END
