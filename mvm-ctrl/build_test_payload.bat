@echo off
setlocal

set RT_DIR=%~dp0..\mvm-payload
set TEST_SRC=%RT_DIR%\tests\test_all.mvm.cpp
set OUT_DIR=%~dp0.
:: %1 = full path to mvm_encrypt.exe (passed by the svmctrl PreBuildEvent).
:: When run stand-alone from a developer prompt, this is empty and the
:: encryption step is skipped -- the HV will then refuse the payload,
:: which is intentional (see docs/xor_encrypt_flow.md if you need to add
:: it back).
set MVM_ENCRYPT=%~1

echo Building test payload...

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\microvm_rt.obj" "%RT_DIR%\src\microvm_rt.mvm.cpp"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\microvm_mem.obj" "%RT_DIR%\src\microvm_mem.mvm.cpp"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\microvm_proc.obj" "%RT_DIR%\src\microvm_proc.mvm.cpp"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\microvm_mod.obj" "%RT_DIR%\src\microvm_mod.mvm.cpp"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\microvm_scan.obj" "%RT_DIR%\src\microvm_scan.mvm.cpp"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%\include" ^
    /Fo"%OUT_DIR%\test_all.obj" "%TEST_SRC%"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

ml64.exe /c /nologo /Fo"%OUT_DIR%\microvm_vmmcall.obj" "%RT_DIR%\src\microvm_vmmcall.asm"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

:: /IGNORE:4108,4281 -- payload is loaded at a fixed guest PA, not a Windows driver / ASLR target.
link.exe /nologo /ENTRY:"_start" /SUBSYSTEM:NATIVE /NODEFAULTLIB /ALIGN:4096 ^
    /BASE:0x10000000 /IGNORE:4108,4281 /OUT:"%OUT_DIR%\test_payload.exe" ^
    "%OUT_DIR%\microvm_vmmcall.obj" ^
    "%OUT_DIR%\microvm_rt.obj" "%OUT_DIR%\microvm_mem.obj" "%OUT_DIR%\microvm_proc.obj" ^
    "%OUT_DIR%\microvm_mod.obj" "%OUT_DIR%\microvm_scan.obj" ^
    "%OUT_DIR%\test_all.obj"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

python "%~dp0..\tools\extract_text_and_bin2h.py" "%OUT_DIR%\test_payload.exe" "%OUT_DIR%\test_payload.mvm.h" "test_payload_bin"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

if "%MVM_ENCRYPT%"=="" (
    echo WARNING: mvm_encrypt.exe path not provided; test_payload.mvm.h left as plaintext.
    echo          The HV will refuse this payload. Run from MSBuild ^(svmctrl PreBuildEvent^)
    echo          which passes the built exe path as %%1.
    exit /b 0
)
if not exist "%MVM_ENCRYPT%" (
    echo ERROR: mvm_encrypt.exe not found at %MVM_ENCRYPT%
    exit /b 1
)

"%MVM_ENCRYPT%" "%OUT_DIR%\test_payload.mvm.h"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Done. Generated test_payload.mvm.h (pre-encrypted; key in test_payload.mvm_key.h)
