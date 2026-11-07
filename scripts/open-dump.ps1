# Opens a Windows crash dump in a debugger with a symbol store (Phase 13 Day 156; the Linux script is open-core.sh).
#
#   scripts/open-dump.ps1 DUMP-OR-ENVELOPE [SYMBOL-STORE] [-Batch]
#
# Uses WinDbg (WinDbgX.exe, from the Store or winget) or, with -Batch or when it is not installed, cdb.exe (the Debugging Tools for Windows in
# the Windows SDK). An envelope's minidump is extracted to a temporary file first (crash_extract). The symbol path is the store alone,
# `srv*<store>` with no server after it, and _NT_SYMBOL_PATH is cleared for the debugger, so nothing is fetched from the network.
# With -Batch, cdb prints every thread's stack and the modules and exits (for a script or a test).
# Exit status: the debugger's; 64 bad usage; 66 no debugger installed; 2 other failure.
# Environment: ENGINE_CRASH_EXTRACT is the path of the crash_extract tool (default: found under build-msvc or build, or on PATH).
param(
    [Parameter(Position = 0)] [string] $Dump,
    [Parameter(Position = 1)] [string] $Store,
    [switch] $Batch
)
$ErrorActionPreference = 'Stop'

function Find-CrashExtract {
    if ($env:ENGINE_CRASH_EXTRACT) { return $env:ENGINE_CRASH_EXTRACT }
    $root = Split-Path -Parent $PSScriptRoot
    foreach ($candidate in @("$root\build-msvc\tools\crash_extract\crash_extract.exe", "$root\build\tools\crash_extract\crash_extract.exe")) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $onPath = Get-Command crash_extract.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

if (-not $Dump -or -not (Test-Path -LiteralPath $Dump -PathType Leaf) -or ($Store -and -not (Test-Path -LiteralPath $Store -PathType Container))) {
    [Console]::Error.WriteLine('usage: open-dump.ps1 DUMP-OR-ENVELOPE [SYMBOL-STORE] [-Batch]')
    exit 64
}
$cdb = $null
foreach ($candidate in @("${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe", "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe")) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $cdb = $candidate; break }
}
$windbg = (Get-Command WinDbgX.exe -ErrorAction SilentlyContinue).Source
if (-not $cdb -and ($Batch -or -not $windbg)) {
    [Console]::Error.WriteLine('open-dump.ps1: no debugger was found. Install WinDbg (winget install Microsoft.WinDbg) or the Debugging Tools for Windows (a Windows SDK feature), and run this again.')
    exit 66
}

$dumpPath = (Resolve-Path -LiteralPath $Dump).Path
$temporary = $null
$bytes = [System.IO.File]::ReadAllBytes($dumpPath)
if (-not ($bytes.Length -ge 4 -and $bytes[0] -eq 0x4D -and $bytes[1] -eq 0x44 -and $bytes[2] -eq 0x4D -and $bytes[3] -eq 0x50)) {
    $tool = Find-CrashExtract
    if (-not $tool) {
        [Console]::Error.WriteLine("open-dump.ps1: $dumpPath is not a minidump, and the crash_extract tool was not found (build the project, or set ENGINE_CRASH_EXTRACT)")
        exit 2
    }
    $temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("open-dump-" + [guid]::NewGuid().ToString() + '.dmp')
    & $tool $dumpPath $temporary | Out-Null
    if ($LASTEXITCODE -ne 0) {
        [Console]::Error.WriteLine("open-dump.ps1: no minidump could be extracted from $dumpPath")
        exit 2
    }
    $dumpPath = $temporary
}
$symbols = if ($Store) { 'srv*' + (Resolve-Path -LiteralPath $Store).Path } else { '' }
$saved = $env:_NT_SYMBOL_PATH
$env:_NT_SYMBOL_PATH = $null
try {
    if ($Batch -or -not $windbg) {
        $arguments = @('-z', $dumpPath)
        if ($symbols) { $arguments += @('-y', $symbols) } # (an empty -y would take the next argument as the path)
        $arguments += @('-c', '.ecxr;kn;~*kn;lm;q')
        & $cdb @arguments
    } else {
        $arguments = @('-z', $dumpPath)
        if ($symbols) { $arguments += @('-y', $symbols) }
        & $windbg @arguments
    }
    $status = $LASTEXITCODE
} finally {
    $env:_NT_SYMBOL_PATH = $saved
    if ($temporary) { Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue }
}
exit $status
