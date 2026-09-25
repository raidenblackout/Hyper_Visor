.CODE

svm_clgi PROC
    clgi
    ret
svm_clgi ENDP

svm_stgi PROC
    stgi
    ret
svm_stgi ENDP

svm_vmload PROC
    mov     rax, rcx
    vmload  rax
    ret
svm_vmload ENDP

svm_vmsave PROC
    mov     rax, rcx
    vmsave  rax
    ret
svm_vmsave ENDP

svm_read_cs PROC
    xor     rax, rax
    mov     ax, cs
    ret
svm_read_cs ENDP

svm_read_ss PROC
    xor     rax, rax
    mov     ax, ss
    ret
svm_read_ss ENDP

svm_read_ds PROC
    xor     rax, rax
    mov     ax, ds
    ret
svm_read_ds ENDP

svm_read_es PROC
    xor     rax, rax
    mov     ax, es
    ret
svm_read_es ENDP

svm_read_fs PROC
    xor     rax, rax
    mov     ax, fs
    ret
svm_read_fs ENDP

svm_read_gs PROC
    xor     rax, rax
    mov     ax, gs
    ret
svm_read_gs ENDP

svm_read_ldtr PROC
    xor     rax, rax
    sldt    ax
    ret
svm_read_ldtr ENDP

svm_read_tr PROC
    xor     rax, rax
    str     ax
    ret
svm_read_tr ENDP

svm_sgdt PROC
    sgdt    fword ptr [rcx]
    ret
svm_sgdt ENDP

svm_sidt PROC
    sidt    fword ptr [rcx]
    ret
svm_sidt ENDP

svm_load_ar PROC
    lar     rax, rcx
    jz      ar_ok
    xor     rax, rax
    ret
ar_ok:
    ret
svm_load_ar ENDP

svm_lgdt PROC
    lgdt    fword ptr [rcx]
    ret
svm_lgdt ENDP

svm_ltr PROC
    ltr     cx
    ret
svm_ltr ENDP

svm_lidt PROC
    lidt    fword ptr [rcx]
    ret
svm_lidt ENDP

END
