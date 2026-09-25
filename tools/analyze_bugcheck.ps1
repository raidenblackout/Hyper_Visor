# analyze_bugcheck.ps1 -- capture the 0x7E bugcheck details for the Type-1 HV work.
#
# RUN THIS YOURSELF from an elevated PowerShell after a 0x7E boot attempt.
# It writes a log file and prints its path; send me that file.
#
#   powershell -ExecutionPolicy Bypass -File e:\Projects\Hyper_Visor\tools\analyze_bugcheck.ps1
#
# What we need from it: bugcheck parameter 1 (the exception code) and
# parameter 2 (the faulting address). Those split the remaining candidates:
#   0xC000001D illegal instruction -> exception injection / #UD path
#   0xC0000005 access violation    -> NPT coverage or guest-state corruption
#   0x80000003 breakpoint          -> #DB / ICEBP intercept

$ErrorActionPreference = 'Continue'
$out = Join-Path $PSScriptRoot 'bugcheck_analysis.log'
"=== analyze_bugcheck.ps1 $(Get-Date -Format s) ===" | Set-Content -Encoding utf8 $out

function Log($msg) { $msg | Add-Content -Encoding utf8 $out; Write-Host $msg }

# ---- 1. Locate a debugger -------------------------------------------------
$cdb = $null
$roots = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64",
    "$env:ProgramFiles\Windows Kits\10\Debuggers\x64"
)
foreach ($r in $roots) {
    $c = Join-Path $r 'cdb.exe'
    if (Test-Path $c) { $cdb = $c; break }
}
if ($cdb) { Log "debugger: $cdb" } else { Log "debugger: NOT FOUND (install WinDbg / Debugging Tools for Windows)" }

# ---- 2. Dump configuration ------------------------------------------------
Log ""
Log "--- crash dump configuration ---"
try {
    $cc = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl' -ErrorAction Stop
    Log ("CrashDumpEnabled : {0}  (0=none 1=complete 2=kernel 3=small 7=automatic)" -f $cc.CrashDumpEnabled)
    Log ("AutoReboot       : {0}  (set to 0 to keep the BSOD on screen)" -f $cc.AutoReboot)
    Log ("DumpFile         : {0}" -f $cc.DumpFile)
    Log ("MinidumpDir      : {0}" -f $cc.MinidumpDir)
} catch { Log "could not read CrashControl: $_" }

# ---- 3. Find dumps --------------------------------------------------------
Log ""
Log "--- dump files ---"
$dumps = @()
if (Test-Path "$env:SystemRoot\MEMORY.DMP") { $dumps += "$env:SystemRoot\MEMORY.DMP" }
$mini = "$env:SystemRoot\Minidump"
if (Test-Path $mini) {
    $dumps += (Get-ChildItem $mini -Filter *.dmp -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 3 |
               ForEach-Object { $_.FullName })
}
if ($dumps.Count -eq 0) {
    Log "NO DUMPS FOUND."
    Log ""
    Log "The bugcheck likely happens before the disk stack can write one,"
    Log "or the machine was power-cycled before the dump could be flushed."
    Log ""
    Log "From an elevated PowerShell (wmic is REMOVED on Win11 24H2+, use these):"
    Log "  Set-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl' AutoReboot 0"
    Log "  Set-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl' CrashDumpEnabled 1"
    Log ""
    Log "Then reproduce, PHOTOGRAPH the bugcheck screen, and let the machine sit."
    Log "Do NOT power-cycle: the dump is written during the bugcheck itself, so"
    Log "cutting power discards it and leaves only older, unrelated dumps."
} else {
    foreach ($d in $dumps) { Log "found: $d" }
}

# ---- 4. Analyze -----------------------------------------------------------
if ($cdb -and $dumps.Count -gt 0) {
    $target = $dumps[0]
    Log ""
    Log "--- !analyze -v on $target ---"
    $cmds = '.symfix; .reload; !analyze -v; q'
    & $cdb -z $target -c $cmds 2>&1 | Add-Content -Encoding utf8 $out
}

# ---- 5. Recent bugcheck events -------------------------------------------
Log ""
Log "--- recent BugCheck events (System log, id 1001) ---"
try {
    Get-WinEvent -FilterHashtable @{LogName='System'; Id=1001} -MaxEvents 5 -ErrorAction Stop |
        ForEach-Object { Log ("[{0}] {1}" -f $_.TimeCreated, ($_.Message -replace '\s+', ' ')) }
} catch { Log "no BugCheck events found ($_)" }

Log ""
Log "=== done -> $out ==="
Write-Host ""
Write-Host "Send me this file: $out" -ForegroundColor Cyan
