param([string]$OutputDirectory = 'D:/OS/kernel/build/clock-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ G4 kernel clock ============

$saved = Use-Msvc
try {
    $sources = @('kernel/time/clock.c', 'test/kernel/time/clock_test.c') | ForEach-Object { "$project/$_" }
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi', '/fsanitize=address', '/MD', '/O2', "/I$project")
    Invoke-Checked "$($saved.Bin)/cl.exe" ($flags + $sources + @("/Fo$out/", "/Fd$out/host.pdb", "/Fe$out/host.exe", '/link', '/incremental:no'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out $sources
} finally {
    Restore-Msvc $saved
}
"PASS: kernel clock evidence $out"
