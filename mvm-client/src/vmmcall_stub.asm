.CODE

svm_client_vmmcall PROC
    mov     r10, [rsp+28h]
    vmmcall
    ret
svm_client_vmmcall ENDP

END
