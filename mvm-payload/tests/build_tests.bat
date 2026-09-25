@echo off
setlocal

set RT_DIR=%~dp0..
set TEST_SRC=%~dp0test_all.mvm.cpp

echo [1/4] Compiling runtime library + test payload (freestanding)...
cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /W4 /std:c++17 /I"%RT_DIR%\include" ^
    "%RT_DIR%\src\microvm_rt.mvm.cpp" ^
    "%RT_DIR%\src\microvm_mem.mvm.cpp" ^
    "%RT_DIR%\src\microvm_proc.mvm.cpp" ^
    "%RT_DIR%\src\microvm_mod.mvm.cpp" ^
    "%RT_DIR%\src\microvm_scan.mvm.cpp" ^
    "%TEST_SRC%"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [2/4] Assembling VMMCALL stubs + entry point...
ml64.exe /c /nologo /Fo"microvm_vmmcall.obj" "%RT_DIR%\src\microvm_vmmcall.asm"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [3/4] Linking into minimal PE...
link.exe /nologo /ENTRY:"_start" /SUBSYSTEM:NATIVE /NODEFAULTLIB /ALIGN:4096 ^
    /BASE:0x180000000 /IGNORE:4078,4197,4254,4010,4044 /OUT:"test_all.exe" ^
    microvm_vmmcall.obj microvm_rt.obj microvm_mem.obj microvm_proc.obj ^
    microvm_mod.obj microvm_scan.obj test_all.obj
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [4/4] Extracting .text section to header...
python "%RT_DIR%\..\tools\extract_text_and_bin2h.py" test_all.exe test_all.mvm.h test_all_bin
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Done. Generated test_all.mvm.h
