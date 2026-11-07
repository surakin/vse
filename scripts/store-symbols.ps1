# Files a Windows program's symbols in a symbol store, under its build id (Phase 13 Day 156; the Linux script is store-symbols.sh).
#
#   scripts/store-symbols.ps1 BINARY [STORE]        BINARY is an .exe or .dll linked with /DEBUG; STORE defaults to .\symbols
#
# The layout is the one WinDbg, cdb and Visual Studio read from a local store (symbol path `srv*STORE`), and scripts/crash-report.ps1:
#   STORE\<name>.pdb\<GUID+age>\<name>.pdb       the PDB, under the build id the crash context and the minidump record
#   STORE\<name>.exe\<timestamp><size>\<name>.exe the image itself, which a debugger needs to read a minidump's modules
# The PDB is the one the image names, or <name>.pdb next to it. Files are only added; nothing in the store is removed. Keep the store for as
# long as you keep the builds it belongs to. Prints the build id and the two stored paths.
# Exit status: 0 stored; 2 failure (no CodeView record, no PDB, a copy failed); 64 bad usage.
# Environment: ENGINE_CRASH_EXTRACT is the path of the crash_extract tool (default: found under build-msvc or build, or on PATH).
param(
    [Parameter(Position = 0)] [string] $Binary,
    [Parameter(Position = 1)] [string] $Store = 'symbols'
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

if (-not $Binary -or -not (Test-Path -LiteralPath $Binary -PathType Leaf)) {
    [Console]::Error.WriteLine('usage: store-symbols.ps1 BINARY [STORE]')
    exit 64
}
$tool = Find-CrashExtract
if (-not $tool) {
    [Console]::Error.WriteLine('store-symbols.ps1: the crash_extract tool was not found (build the project, or set ENGINE_CRASH_EXTRACT)')
    exit 2
}
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$info = & $tool --image $binaryPath 2>&1
if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine("store-symbols.ps1: $info")
    exit 2
}
$fields = @{}
foreach ($line in $info) {
    if ("$line" -match '^([a-z ]+): (.*)$') { $fields[$Matches[1]] = $Matches[2] }
}
$id = $fields['build id']
$pdbEntry = $fields['pdb store entry']
$imageEntry = $fields['image store entry']
if (-not $id -or -not $pdbEntry -or -not $imageEntry) {
    [Console]::Error.WriteLine("store-symbols.ps1: unexpected output from crash_extract --image: $info")
    exit 2
}
# The PDB the image names, or the one next to it.
$pdbName = Split-Path -Leaf $pdbEntry
$pdb = $fields['debug file']
if (-not $pdb -or -not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
    $pdb = Join-Path (Split-Path -Parent $binaryPath) $pdbName
}
if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
    [Console]::Error.WriteLine("store-symbols.ps1: no PDB for $binaryPath (looked for $($fields['debug file']) and $pdb)")
    exit 2
}
try {
    $targets = @()
    foreach ($pair in @(@($pdb, $pdbEntry), @($binaryPath, $imageEntry))) {
        $target = Join-Path $Store ($pair[1] -replace '/', '\')
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $pair[0] -Destination $target -Force
        $targets += (Resolve-Path -LiteralPath $target).Path
    }
} catch {
    [Console]::Error.WriteLine("store-symbols.ps1: $($_.Exception.Message)")
    exit 2
}
Write-Output $id
$targets | ForEach-Object { Write-Output $_ }
exit 0
