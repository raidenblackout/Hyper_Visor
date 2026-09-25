"""Test the staged CLI with a fake backend. Never links the hypercall client."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
VSVARS = r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

HARNESS = r'''

int cmd_microvm_check(int, LPWSTR*);
static std::vector<int64_t> results;
static size_t cursor;
static int creates, loads, destroys, mode;
namespace mvm {
bool hypervisor_present() { return mode != 1; }
uint64_t microvm_create(uint64_t) { ++creates; return mode == 2 ? 0 : 1; }
bool microvm_load(uint64_t,const void*,uint64_t,uint64_t,uint64_t,uint8_t) { ++loads; return mode != 3; }
int64_t microvm_step(uint64_t) { return cursor < results.size() ? results[cursor++] : 0; }
bool microvm_destroy(uint64_t) { ++destroys; return mode != 4; }
}
int main() {
    bool ok = true;
    auto check = [&](const wchar_t* name, std::vector<int64_t> seq, int failure,
                     bool expect_ok, int expected_loads, int expected_destroys) {
        results=seq; cursor=0; creates=loads=destroys=0; mode=failure;
        wchar_t a[]=L"mvm-ctrl", b[]=L"microvm-check";
        LPWSTR args[]={a,b,const_cast<wchar_t*>(name)};
        int rc=cmd_microvm_check(3,args);
        bool pass=(rc==0)==expect_ok && loads==expected_loads && destroys==expected_destroys;
        std::printf("OFFLINE %s: %ls mode=%d\n",pass?"PASS":"FAIL",name,failure);
        ok &= pass;
    };
    check(L"yield",{0,0,0,1,1},0,true,1,1);
    check(L"fault",{0,-1,-1,0,1,1},0,true,2,1);
    check(L"yield",{},0,false,1,1); // Step budget exhausted.
    check(L"yield",{0,-1},0,false,1,1);
    check(L"yield",{0,0,0,1,-1},0,false,1,1); // Completed status lost.
    check(L"fault",{1},0,false,1,1); // Fault was not reported.
    check(L"fault",{-2},0,false,1,1); // Shutdown is not the expected fault result.
    check(L"fault",{-1,0},0,false,1,1); // Fault status lost.
    check(L"fault",{-1,-1,-1},0,false,2,1); // Reload failed to reset status.
    check(L"yield",{},1,false,0,0);
    check(L"yield",{},2,false,0,0);
    check(L"yield",{},3,false,1,1);
    check(L"yield",{0,0,0,1,1},4,false,1,1);
    check(L"unknown",{},0,false,0,0);
    return ok?0:1;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='microvm-check-', ignore_cleanup_errors=True) as tmp:
        folder = pathlib.Path(tmp)
        (folder / 'test.cpp').write_text(HARNESS)
        (folder / 'check.cmd').write_text(
            f'@echo off\ncall "{VSVARS}" >nul\nif errorlevel 1 exit /b 1\n'
            f'ml64 /nologo /c /Fopayload.obj "{ROOT / "mvm-ctrl/src/microvm_payload.asm"}"\n'
            'if errorlevel 1 exit /b 1\n'
            f'cl /nologo /EHsc /std:c++17 /W4 /WX /I"{ROOT / "mvm-client/include"}" '
            f'/I"{ROOT / "mvm-hv/include"}" /I"{ROOT / "mvm-ctrl/include"}" '
            f'test.cpp "{ROOT / "mvm-ctrl/src/cmd_microvm_check.cpp"}" payload.obj /Fe:check.exe\n'
            'if errorlevel 1 exit /b 1\ncheck.exe\n')
        return subprocess.run(['cmd', '/c', 'check.cmd'], cwd=folder).returncode

if __name__ == '__main__':
    raise SystemExit(main())
