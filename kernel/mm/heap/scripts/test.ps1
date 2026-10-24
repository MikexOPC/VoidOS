param([string]$OutputDirectory = 'D:/OS/kernel/build/heap-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ G3 kernel heap core ============

$saved = Use-Msvc
try {
    $sources = @('kernel/mm/heap/heap.c', 'lib/containers/bitmap.c', 'test/kernel/mm/heap/heap_test.c') | ForEach-Object { "$project/$_" }
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi', '/fsanitize=address', '/MD', '/O2', "/I$project")
    Invoke-Checked "$($saved.Bin)/cl.exe" ($flags + $sources + @("/Fo$out/", "/Fd$out/host.pdb", "/Fe$out/host.exe", '/link', '/incremental:no'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out $sources
} finally {
    Restore-Msvc $saved
}
"PASS: kernel heap evidence $out"
