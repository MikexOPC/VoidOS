param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
if (!$OutputDirectory) { $OutputDirectory = Join-Path $project ('kernel/build/heap-sync-' + [guid]::NewGuid().ToString('N')) }
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ G3 kernel heap core ============

$saved = Use-Msvc
try {
    $sources = @('kernel/mm/heap/heap.c','kernel/mm/heap/core/state.c','kernel/mm/heap/core/reserve.c','kernel/mm/heap/core/release.c','kernel/mm/heap/core/guard.c','kernel/mm/heap/core/fill.c', 'lib/containers/bitmap.c', 'test/kernel/mm/heap/heap_test.c') | ForEach-Object { "$project/$_" }
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $syncObjects = @(New-PhysicalHostSync $project $out)
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi', '/fsanitize=address', '/MD', '/O2', "/I$project")
    Invoke-Checked "$($saved.Bin)/cl.exe" ($flags + $sources + @("/Fo$out/", "/Fd$out/host.pdb", "/Fe$out/host.exe", '/link', '/incremental:no') + $syncObjects)
    Invoke-Checked "$out/host.exe" @()
    $core = @($sources | Where-Object { $_ -notlike '*heap_test.c' })
    $syncObjects = @(New-PhysicalHostSync $project $out -Threaded)
    foreach ($case in @('contract', 'threads', 'transfer')) {
        $caseFlags = @('/UNDEBUG')
        if ($case -eq 'transfer') { $caseFlags += '/DKERNEL_HEAP_TEST_FILL=1' }
        $target = "$out/$case"
        $caseSources = $core + @("$project/test/kernel/mm/heap/synchronization/$case.c")
        Invoke-Checked "$($saved.Bin)/cl.exe" ($flags + $caseFlags + $caseSources +
            @("/Fo$out/", "/Fd$target.pdb", "/Fe$target.exe", '/link', '/incremental:no', '/WX') + $syncObjects)
        if ($case -eq 'threads') {
            foreach ($count in @(2, 4, 8)) { Invoke-Checked "$target.exe" @([string]$count) }
        } else { Invoke-Checked "$target.exe" @() }
        New-Item -ItemType Directory -Force "$out/$case-analysis" | Out-Null
        Invoke-Cppcheck $project "$out/$case-analysis" $caseSources
    }
    Invoke-Cppcheck $project $out $sources
} finally {
    Restore-Msvc $saved
}
"PASS: kernel heap evidence $out"
