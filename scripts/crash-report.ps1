# Turns a Windows crash dump into a readable trace (Phase 13 Day 156; the Linux script is crash-report.sh).
#
#   scripts/crash-report.ps1 DUMP-OR-ENVELOPE [EXECUTABLE] [SYMBOL-STORE] [-Report REPORT]
#
# DUMP-OR-ENVELOPE is the .envelope file a crash leaves in the dump directory (scripts/find-dump.ps1 lists them) or a minidump (.dmp). The
# other arguments, in any order: the program that crashed (a file), and a symbol store made by scripts/store-symbols.ps1 (a directory).
#
# Prints the exception and the modules with their build ids (crash_extract --modules), then the engine's crash report: from the envelope, or
# REPORT, or the crash-report.txt beside the dump. With a store, the report's frames (module+offset) are named with function, file and line
# (crash_extract --resolve, DbgHelp). With the program, its build id is compared with the dump's: a different build gives wrong names, so the
# script says so and exits 3. Nothing is read from the network: the only symbol path is the store.
# Exit status: 0 ok, 3 build id mismatch, 64 bad usage, 2 other failure.
# Environment: ENGINE_CRASH_EXTRACT is the path of the crash_extract tool (default: found under build-msvc or build, or on PATH).
param(
    [Parameter(Position = 0)] [string] $Dump,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)] [string[]] $Rest = @(),
    [string] $Report
)
$ErrorActionPreference = 'Stop'

function Find-CrashExtract {
    if ($env:ENGINE_CRASH_EXTRACT) { return $env:ENGINE_CRASH_EXTRACT }
    $root = Split-Path -Parent $PSScriptRoot
    foreach ($candidate in @("$root\build-msvc\tools\crash_extract\crash_extract.exe", "$root\build\tools\crash_extract\crash_extract.exe",
                              ".\build-msvc\tools\crash_extract\crash_extract.exe", ".\build\tools\crash_extract\crash_extract.exe")) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $onPath = Get-Command crash_extract.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

function Fail([int] $status, [string] $text) {
    [Console]::Error.WriteLine("crash-report.ps1: $text")
    exit $status
}

if (-not $Dump -or -not (Test-Path -LiteralPath $Dump -PathType Leaf)) {
    [Console]::Error.WriteLine('usage: crash-report.ps1 DUMP-OR-ENVELOPE [EXECUTABLE] [SYMBOL-STORE] [-Report REPORT]')
    exit 64
}
$exe = $null
$store = $null
foreach ($arg in $Rest) {
    if (Test-Path -LiteralPath $arg -PathType Container) { $store = (Resolve-Path -LiteralPath $arg).Path }
    elseif (Test-Path -LiteralPath $arg -PathType Leaf) { $exe = (Resolve-Path -LiteralPath $arg).Path }
    else { Fail 64 "no such file or directory: $arg" }
}
if ($Report -and -not (Test-Path -LiteralPath $Report -PathType Leaf)) { Fail 64 "no such report: $Report" }
$tool = Find-CrashExtract
if (-not $tool) { Fail 2 'the crash_extract tool was not found (build the project, or set ENGINE_CRASH_EXTRACT)' }
$dumpPath = (Resolve-Path -LiteralPath $Dump).Path

$modules = & $tool --modules $dumpPath 2>&1
if ($LASTEXITCODE -ne 0) { Fail 2 "$modules" }

# The program given: its build id against the dump's module of the same name.
if ($exe) {
    $image = & $tool --image $exe 2>&1
    if ($LASTEXITCODE -ne 0) { Fail 2 "$image" }
    $want = (($image | Where-Object { "$_" -like 'build id: *' }) -replace '^build id: ', '').Trim()
    $name = Split-Path -Leaf $exe
    $line = $modules | Where-Object { "$_" -match '^module: \S+ \S+ (\S+) (.*)$' -and (Split-Path -Leaf $Matches[2]) -ieq $name } | Select-Object -First 1
    if (-not $line) {
        [Console]::Error.WriteLine("crash-report.ps1: warning: the dump lists no module named $name; its build id was not compared")
    } else {
        $null = "$line" -match '^module: \S+ \S+ (\S+) '
        $got = $Matches[1]
        if ($got -ne '-' -and $got -ne $want) {
            Fail 3 "the dump was made by a different build than $exe (dump: $got, program: $want); the trace would be wrong"
        }
    }
}

# The report: the given file, the envelope's own, or crash-report.txt beside the dump (or in the folder above a <uuid>.run folder).
$reportPath = $null
if ($Report) {
    $reportPath = (Resolve-Path -LiteralPath $Report).Path
} else {
    $attached = & $tool --report $dumpPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        $dir = Split-Path -Parent $dumpPath
        foreach ($candidate in @((Join-Path $dir 'crash-report.txt'), (Join-Path (Split-Path -Parent $dir) 'crash-report.txt'))) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $reportPath = $candidate; break }
        }
    }
}

Write-Output '== exception and modules (crash_extract) =='
$modules | ForEach-Object { Write-Output "$_" }
if ($store) {
    Write-Output '== the crash report, frames named from the symbol store =='
    $resolveArgs = @('--resolve', $store, $dumpPath)
    if ($reportPath) { $resolveArgs += $reportPath }
    $named = & $tool @resolveArgs 2>&1
    $status = $LASTEXITCODE
    $named | ForEach-Object { Write-Output "$_" }
    if ($status -eq 3) {
        [Console]::Error.WriteLine("crash-report.ps1: warning: no frame was found in $store; store the program's symbols with scripts/store-symbols.ps1")
    } elseif ($status -eq 1) {
        [Console]::Error.WriteLine('crash-report.ps1: warning: no crash report was found for this dump (give one with -Report)')
    } elseif ($status -ne 0) {
        Fail 2 'the report could not be read'
    }
} else {
    Write-Output '== the crash report (give a symbol store to name the frames) =='
    if ($reportPath) { Get-Content -LiteralPath $reportPath | ForEach-Object { Write-Output $_ } }
    elseif ($attached) { $attached | ForEach-Object { Write-Output "$_" } }
    else { [Console]::Error.WriteLine('crash-report.ps1: warning: no crash report was found for this dump') }
}
exit 0
