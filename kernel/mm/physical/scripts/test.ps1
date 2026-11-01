param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/physical-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ allocator policy and alignment ============

$objects = @()
foreach ($entry in @(@('kernel/mm/rust/regions/validation.rs','regions'),
    @('kernel/mm/physical/rust/alignment/placement.rs','placement'))) {
    $object = "$out/$($entry[1]).obj"
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',"$project/$($entry[0])",'-o',$object)
    $objects += $object
}
$test = "$project/test/kernel/mm/physical/rust/alignment/placement_test.rs"
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',$test,'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/$mode.exe" @()
}
Invoke-Checked clippy-driver @('--edition=2024','--test','-D','warnings','--emit=metadata',$test,'-o',"$out/check.rmeta")

# ============ C ownership rollback and accounting under ASan ============

$sources = @('kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c',
    'lib/memory/address_ranges.c','test/kernel/mm/physical/allocator_test.c','test/kernel/mm/physical/alignment_test.c',
    'test/kernel/mm/physical/synchronization/queries_test.c')
$runner = @'
#include <stdio.h>
int run_physical_tests(void);
int run_alignment_tests(void);
int run_physical_query_tests(void);
int main(void) {
    for (unsigned int iteration = 0; iteration < 10; ++iteration) {
        int result = run_physical_tests();
        if (result) { printf("physical failure %d\n", result); return 1; }
        result = run_alignment_tests();
        if (result) { printf("alignment failure %d\n", result); return 1; }
        result = run_physical_query_tests();
        if (result) return 1;
    }
    puts("PHYSICAL_ASAN_PASS: 10 repetitions, ownership rollback coalescing and accounting");
    return 0;
}
'@
[IO.File]::WriteAllText("$out/runner.c",$runner,[Text.UTF8Encoding]::new($false))
$saved = Use-Msvc
try {
    $paths = @($sources | ForEach-Object { "$project/$_" })
    foreach ($source in ($paths + @("$out/runner.c"))) {
        $object = "$out/" + (([IO.Path]::GetFileNameWithoutExtension($source)) + '-c.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',"/I$project","/Fo$object","/Fd$out/host.pdb",$source)
        $objects += $object
    }
    $physicalObjects = @($objects | Where-Object {
        [IO.Path]::GetFileName($_) -notin @('allocator_test-c.obj','alignment_test-c.obj','queries_test-c.obj','runner-c.obj')
    })
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $objects += @(New-PhysicalHostSync $project $out)
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()

    # ============ real contention with one modeled CPU context per host thread ============

    $threadSync = @(New-PhysicalHostSync $project $out -Threaded)
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/UNDEBUG','/c',"/I$project","/Fo$out/threads.obj","/Fd$out/threads.pdb",
        "$project/test/kernel/mm/physical/synchronization/threads.c")
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/threads.exe") +
        $physicalObjects + @("$out/threads.obj") + $threadSync + @('/link','/incremental:no','/DEBUG','/WX'))
    foreach ($count in @(2, 4, 8)) { Invoke-Checked "$out/threads.exe" @([string]$count) }
    Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" `
        --quiet --platform=win64 --std=c11 --xml --xml-version=2 "-I$project" @paths
    if ($LASTEXITCODE) { throw 'Cppcheck execution failed' }
    [xml]$report = Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object { $_.severity -in 'error','warning','performance','portability' }).Count) {
        throw 'Blocking static diagnostics'
    }
} finally { Restore-Msvc $saved }
"PASS: physical core evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
