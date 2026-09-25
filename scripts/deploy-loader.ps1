#requires -RunAsAdministrator
# See docs/DEPLOY.md for full deployment workflow.

[CmdletBinding()]
param(
    [switch]$LogsOnly,
    # BuildDir defaults to $PSScriptRoot when the script is running from the
    # build output dir (bin/x64/Release/, where mvm-hv.vcxproj mirrors it via
    # the CopyDeployScript target), and to ../bin/x64/Release/ when running
    # from the tracked scripts/ location.
    [string]$BuildDir = $(if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'loader.efi')) {
                            $PSScriptRoot
                        } else {
                            Join-Path $PSScriptRoot '..\bin\x64\Release'
                        })
)

$ErrorActionPreference = 'Stop'

$BuildDir  = (Resolve-Path -LiteralPath $BuildDir).Path
$loaderSrc = Join-Path $BuildDir 'loader.efi'
$binSrc    = Join-Path $BuildDir 'mvm-hv.bin'

if (-not $LogsOnly) {
    if (-not (Test-Path -LiteralPath $loaderSrc)) {
        throw "loader.efi not found in BuildDir: $loaderSrc"
    }
    $binPresent = Test-Path -LiteralPath $binSrc
    if (-not $binPresent) {
        Write-Warning "mvm-hv.bin not found in BuildDir: $binSrc (loader-only deploy)"
    }
}

# diskpart returns 0 for benign failures; verify by testing S:\ below.
$dpScript = @'
select volume 2
assign letter=S
'@
$dpFile = Join-Path $env:TEMP ("assign-s-{0}.txt" -f ([guid]::NewGuid()))
Set-Content -LiteralPath $dpFile -Value $dpScript -Encoding ASCII
try {
    Write-Host "Running diskpart to assign S: to volume 2..."
    & diskpart /s $dpFile | Out-Host
} finally {
    Remove-Item -LiteralPath $dpFile -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath 'S:\')) {
    throw "S:\ is not accessible after diskpart. Verify that volume 2 exists and is the intended ESP."
}

$destDir = 'S:\EFI\mvm'
if (-not (Test-Path -LiteralPath $destDir)) {
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
}

if (-not $LogsOnly) {
    $loaderDest = Join-Path $destDir 'loader.efi'
    Copy-Item -LiteralPath $loaderSrc -Destination $loaderDest -Force
    Write-Host "Copied $loaderSrc -> $loaderDest"

    if ($binPresent) {
        $binDest = Join-Path $destDir 'mvm-hv.bin'
        Copy-Item -LiteralPath $binSrc -Destination $binDest -Force
        Write-Host "Copied $binSrc -> $binDest"
    }

    $netlogSrc = Join-Path $BuildDir 'netlog.cfg'
    if (Test-Path -LiteralPath $netlogSrc) {
        $netlogDest = Join-Path $destDir 'netlog.cfg'
        Copy-Item -LiteralPath $netlogSrc -Destination $netlogDest -Force
        Write-Host "Copied $netlogSrc -> $netlogDest"
    }
}


# sbrun.bin / mod2.bin (loader image dumps) and hvb_prev_mailbox.txt are no
# longer pulled back.
$logNames = @('hv-live.log', 'hvb_addr.txt')

$logsRoot = Join-Path $BuildDir 'logs'
$stamp    = Get-Date -Format 'yyyyMMdd-HHmmss'
$logsDir  = Join-Path $logsRoot $stamp
New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

$anyPulled = $false
foreach ($n in $logNames) {
    # Look in \EFI\mvm first, then ESP root (earlier loaders wrote sbrun.bin there).
    $src = Join-Path $destDir $n
    if (-not (Test-Path -LiteralPath $src)) {
        $rootSrc = Join-Path 'S:\' $n
        if (Test-Path -LiteralPath $rootSrc) { $src = $rootSrc }
    }
    if (Test-Path -LiteralPath $src) {
        $dst = Join-Path $logsDir $n
        Copy-Item -LiteralPath $src -Destination $dst -Force
        $bytes = (Get-Item -LiteralPath $dst).Length
        Write-Host ("Pulled {0} ({1} bytes) -> {2}" -f $n, $bytes, $dst)
        $anyPulled = $true
    } else {
        Write-Host ("Skipped {0} (not present on ESP)" -f $n)
    }
}

if ($anyPulled) {
    $latestDir = Join-Path $logsRoot 'latest'
    if (Test-Path -LiteralPath $latestDir) {
        Remove-Item -LiteralPath $latestDir -Recurse -Force
    }
    Copy-Item -LiteralPath $logsDir -Destination $latestDir -Recurse -Force
    Write-Host "Latest snapshot mirrored -> $latestDir"
} else {
    Write-Warning "No log files found on ESP under $destDir"
    Remove-Item -LiteralPath $logsDir -Force -ErrorAction SilentlyContinue
}
