# Lists the crash dumps a Windows program left, newest first (Phase 13 Day 156; the Linux script is find-core.sh).
#
#   scripts/find-dump.ps1 APP
#
# APP is the application name the crash handler used (ENGINE_APP_NAME, or the executable's name without .exe). Two places are searched:
#   %LOCALAPPDATA%\APP\crashes                  the engine's envelopes: saved\*.envelope and the <uuid>.run folders of the last run
#   Windows Error Reporting's local dumps       the DumpFolder of the LocalDumps key for APP.exe (HKLM), or %LOCALAPPDATA%\CrashDumps
# Read-only: nothing is moved or removed. Give a listed .envelope to scripts/crash-report.ps1.
# Exit status: 0 at least one found, 1 none, 64 bad usage.
param([Parameter(Position = 0)] [string] $App)
$ErrorActionPreference = 'Stop'
if (-not $App -or $App -match '[\\/:*?"<>|]' -or $App -eq '.' -or $App -eq '..') {
    [Console]::Error.WriteLine('usage: find-dump.ps1 APP   (the application name: no path separators)')
    exit 64
}
$found = @()
$engine = Join-Path $env:LOCALAPPDATA "$App\crashes"
if (Test-Path -LiteralPath $engine -PathType Container) {
    $found += Get-ChildItem -LiteralPath $engine -Recurse -File -Filter '*.envelope' -ErrorAction SilentlyContinue |
        ForEach-Object { [pscustomobject]@{ Kind = 'engine'; Time = $_.LastWriteTime; Path = $_.FullName } }
}
$wer = Join-Path $env:LOCALAPPDATA 'CrashDumps'
foreach ($key in @("HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$App.exe", 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps')) {
    $folder = (Get-ItemProperty -LiteralPath $key -Name DumpFolder -ErrorAction SilentlyContinue).DumpFolder
    if ($folder) { $wer = [Environment]::ExpandEnvironmentVariables($folder); break }
}
if (Test-Path -LiteralPath $wer -PathType Container) {
    $found += Get-ChildItem -LiteralPath $wer -File -Filter "$App.exe.*.dmp" -ErrorAction SilentlyContinue |
        ForEach-Object { [pscustomobject]@{ Kind = 'wer'; Time = $_.LastWriteTime; Path = $_.FullName } }
}
if ($found.Count -eq 0) {
    [Console]::Error.WriteLine("find-dump.ps1: no dump of $App in $engine or $wer")
    exit 1
}
$found | Sort-Object Time -Descending | ForEach-Object { Write-Output ('{0:yyyy-MM-dd HH:mm:ss}  {1,-6}  {2}' -f $_.Time, $_.Kind, $_.Path) }
exit 0
