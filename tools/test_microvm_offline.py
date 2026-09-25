"""Offline MicroVM checks. Replaces SVM instructions; never calls the hypervisor."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
VSVARS = r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

def main():
    runtime = (ROOT / 'mvm-hv/src/micro_vm/micro_vm.cpp').read_text()
    step = runtime.split('int64_t step(instance_t* inst)', 1)[1].split('} // namespace micro_vm', 1)[0]
    controls = '\n'.join(re.findall(r'v\.ControlArea\.Intercept\w+\s*=[^;]+;', runtime))
    step_cpp = r'''

constexpr uint64_t SVM_INTERCEPT_MISC1_VINTR=1u<<4, SVM_INTERCEPT_MISC1_SHUTDOWN=1u<<31;
constexpr uint64_t SVM_INTERCEPT_MISC2_VMRUN=1, SVM_INTERCEPT_MISC2_VMMCALL=2;
constexpr uint64_t VMEXIT_VMMCALL=0x81, VMEXIT_INTR=0x60, VMEXIT_VINTR=0x64, VMEXIT_SHUTDOWN=0x7f;
constexpr uint64_t VMMCALL_MICROVM_DONE=0xD0EEF00DDEADCAFEull, VMMCALL_MICROVM_YIELD=0xD0EEF00DDEAD0002ull;
struct VMCB {
    struct {uint64_t InterceptMisc1{},InterceptMisc2{},InterceptException{},TlbControl{},VIntr{},NRip{},ExitCode{},ExitInfo1{},ExitInfo2{};} ControlArea;
    struct {uint64_t Rax{},Rip{};} StateSaveArea;
};
struct instance_t {
    bool in_use=true,tlb_flush_pending=true; VMCB* vmcb{}; uint64_t vmcb_pa{},saved_gprs[14]{},saved_fp[64]{};
    uint64_t n_steps{},n_exit_vmmcall_done{},n_exit_vmmcall_yield{},n_exit_intr{},n_exit_vintr{},n_exit_other{};
    int64_t terminal_result{};
    uint64_t last_exit_code{},last_exit_rip{},last_exit_info1{},last_exit_info2{};
};
uint64_t fake_exit=VMEXIT_VMMCALL, entries=0;
uint64_t AsmVmRunMicroVm(uint64_t,uint64_t*,...) {++entries; return fake_exit;}
void SvmAdvanceRip(VMCB* v,uint64_t n) {v->StateSaveArea.Rip=v->ControlArea.NRip ? v->ControlArea.NRip : v->StateSaveArea.Rip+n;}
''' + 'int64_t step(instance_t* inst)' + step + r'''
int main() {
    VMCB v{};
''' + controls + r'''
    bool ok=true;
    auto check=[&](bool pass,const char* label){std::cout<<(pass?"PASS: ":"FAIL: ")<<label<<'\n';ok &=pass;};
    check((v.ControlArea.InterceptMisc1 & SVM_INTERCEPT_MISC1_SHUTDOWN)!=0,"shutdown intercepted");
    check((v.ControlArea.InterceptException & ((1u<<6)|(1u<<8)|(1u<<13)|(1u<<14)))==((1u<<6)|(1u<<8)|(1u<<13)|(1u<<14)),"payload faults intercepted");
    instance_t i{};i.vmcb=&v;v.StateSaveArea.Rax=VMMCALL_MICROVM_YIELD;v.StateSaveArea.Rip=0x1000;
    check(step(&i)==0 && v.StateSaveArea.Rip==0x1003,"yield advances RIP");
    v.ControlArea.NRip=0x2003;
    check(step(&i)==0 && v.StateSaveArea.Rip==0x2003,"yield honors next RIP");
    fake_exit=VMEXIT_SHUTDOWN;
    auto first=step(&i);auto before=entries;
    check(first<0 && step(&i)==first && entries==before,"faulted VM cannot re-enter");
    instance_t done{};done.vmcb=&v;fake_exit=VMEXIT_VMMCALL;v.StateSaveArea.Rax=VMMCALL_MICROVM_DONE;
    auto completed=step(&done);before=entries;
    check(completed==1 && step(&done)==1 && entries==before,"completed VM cannot re-enter");
    return ok?0:1;
}
'''
    source = (ROOT / 'mvm-hv/src/asm/vm_launch.asm').read_text()
    proc = source.split('AsmVmRunMicroVm PROC', 1)[1].split('AsmVmRunMicroVm ENDP', 1)[0]
    proc = re.sub(r'^\s*vmload\s+rax\s*$', '', proc, flags=re.M | re.I)
    proc = re.sub(r'^\s*vmsave\s+rax\s*$', '', proc, flags=re.M | re.I)
    proc = re.sub(r'^\s*vmrun\s+rax\s*$', '\n    pxor xmm6, xmm6\n    paddq xmm7, xmm7\n', proc, flags=re.M | re.I)
    if re.search(r'\b(vmrun|vmsave|vmload|vmmcall)\b', proc, re.I):
        raise RuntimeError('Privileged instruction remains in offline fixture')
    asm = '.CODE\nPUBLIC AsmVmRunMicroVm\nAsmVmRunMicroVm PROC\n' + proc + '''
AsmVmRunMicroVm ENDP
PUBLIC RunContextTest
RunContextTest PROC
    sub rsp, 68h
    movdqu xmmword ptr [rsp+20h], xmm6
    movdqu xmmword ptr [rsp+30h], xmm7
    mov [rsp+40h], r9
    pcmpeqd xmm6, xmm6
    pxor xmm7, xmm7
    call AsmVmRunMicroVm
    mov r9, [rsp+40h]
    movdqu xmmword ptr [r9], xmm6
    movdqu xmmword ptr [r9+10h], xmm7
    movdqu xmm6, xmmword ptr [rsp+20h]
    movdqu xmm7, xmmword ptr [rsp+30h]
    add rsp, 68h
    ret
RunContextTest ENDP
END
'''
    cpp = r'''

extern "C" uint64_t RunContextTest(void*, uint64_t*, void*, void*);
int main() {
    alignas(16) uint8_t guest[512]{};
    alignas(16) uint8_t vmcb[4096]{};
    uint64_t gprs[14]{};
    uint64_t host[4]{};
    uint16_t fcw=0x037f; uint32_t mxcsr=0x1f80;
    std::memcpy(guest, &fcw, 2); std::memcpy(guest+24, &mxcsr, 4);
    uint64_t vector[2]={5,9}; std::memcpy(guest+160+7*16,vector,16);
    RunContextTest(vmcb,gprs,guest,host);
    std::memcpy(vector,guest+160+7*16,16);
    bool ok=host[0]==UINT64_MAX && host[1]==UINT64_MAX && host[2]==0 && host[3]==0
         && vector[0]==10 && vector[1]==18;
    if(ok) {
        RunContextTest(vmcb,gprs,guest,host);
        std::memcpy(vector,guest+160+7*16,16);
        ok=vector[0]==20 && vector[1]==36 && host[0]==UINT64_MAX && host[2]==0;
    }
    std::cout << (ok ? "PASS" : "FAIL") << ": host SIMD preservation and guest resume\n";
    return ok ? 0 : 1;
}
'''
    with tempfile.TemporaryDirectory(prefix='microvm-offline-', ignore_cleanup_errors=True) as tmp:
        folder = pathlib.Path(tmp)
        (folder / 'context.asm').write_text(asm)
        (folder / 'context.cpp').write_text(cpp)
        (folder / 'step.cpp').write_text(step_cpp)
        (folder / 'check.cmd').write_text(
            f'@echo off\ncall "{VSVARS}" >nul\nif errorlevel 1 exit /b 1\n'
            'ml64 /nologo /c /Focontext_asm.obj context.asm\nif errorlevel 1 exit /b 1\n'
            'cl /nologo /EHsc /std:c++17 context.cpp context_asm.obj /Fe:context.exe\n'
            'if errorlevel 1 exit /b 1\ncontext.exe\nif errorlevel 1 exit /b 1\n'
            'cl /nologo /EHsc /std:c++17 step.cpp /Fe:step.exe\nif errorlevel 1 exit /b 1\nstep.exe\n')
        result = subprocess.run(['cmd', '/c', 'check.cmd'], cwd=folder)
        return result.returncode

if __name__ == '__main__':
    raise SystemExit(main())
