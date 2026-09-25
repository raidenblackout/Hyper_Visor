.CODE

extern payload_main : proc

PUBLIC _start
_start PROC
    mov     rcx, rdi
    sub     rsp, 28h
    call    payload_main
    add     rsp, 28h

    jmp     $
_start ENDP

PUBLIC mvm_signal_done
mvm_signal_done PROC
    mov     rax, 0D0EEF00DDEADCAFEh
    vmmcall
    ret
mvm_signal_done ENDP

PUBLIC mvm_signal_yield
mvm_signal_yield PROC
    mov     rax, 0D0EEF00DDEAD0002h
    vmmcall
    ret
mvm_signal_yield ENDP

END
