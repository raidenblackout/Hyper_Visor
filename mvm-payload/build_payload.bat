@echo off
setlocal

:: mvm-payload/build_payload.bat
:: Usage: build_payload.bat <user_payload.mvm.cpp> [output_name]
:: Example: build_payload.bat my_hack.mvm.cpp my_hack
::
:: Compiles the runtime library + user payload into a flat binary header.
:: Requires MSVC x64 Developer Command Prompt (vcvars64).

set USER_SRC=%1
set OUT_NAME=%2
set RT_DIR=%~dp0

if "%USER_SRC%"=="" (
    echo Usage: build_payload.bat ^<payload.mvm.cpp^> [output_name]
    exit /b 1
)
if "%OUT_NAME%"=="" set OUT_NAME=payload

echo [1/4] Compiling runtime library + payload (freestanding)...
cl.exe /c /nologo /O2 /GS- /GR- /EHs-c- /Gy /W4 /std:c++17 /I"%RT_DIR%include" ^
    "%RT_DIR%src\microvm_rt.mvm.cpp" ^
    "%RT_DIR%src\microvm_mem.mvm.cpp" ^
    "%RT_DIR%src\microvm_proc.mvm.cpp" ^
    "%RT_DIR%src\microvm_mod.mvm.cpp" ^
    "%RT_DIR%src\microvm_scan.mvm.cpp" ^
    "%USER_SRC%"
if %ERRORLEVEL% NEQ 0 (
    echo Compile failed!
    exit /b %ERRORLEVEL%
)

echo [2/4] Assembling VMMCALL stubs...
ml64.exe /c /nologo /Fo"microvm_vmmcall.obj" "%RT_DIR%src\microvm_vmmcall.asm"
if %ERRORLEVEL% NEQ 0 (
    echo Assembly failed!
    exit /b %ERRORLEVEL%
)

echo [3/4] Linking into minimal PE...
link.exe /nologo /ENTRY:"_start" /SUBSYSTEM:NATIVE /NODEFAULTLIB /ALIGN:4096 ^
    /BASE:0x180000000 /IGNORE:4078,4197,4254,4010,4044 /MERGE:.rdata=.text /ORDER:@"%RT_DIR%link_order.txt" ^
    /OUT:"%OUT_NAME%.exe" ^
    microvm_vmmcall.obj ^
    microvm_rt.obj microvm_mem.obj microvm_proc.obj microvm_mod.obj ^
    microvm_scan.obj %~n1.obj
if %ERRORLEVEL% NEQ 0 (
    echo Link failed!
    exit /b %ERRORLEVEL%
)

echo [4/4] Extracting .text section to header...
python "%RT_DIR%..\tools\extract_text_and_bin2h.py" "%OUT_NAME%.exe" "%OUT_NAME%.mvm.h" "%OUT_NAME%_bin"
if %ERRORLEVEL% NEQ 0 (
    echo Extraction failed!
    exit /b %ERRORLEVEL%
)

echo Done. Generated %OUT_NAME%.mvm.h
