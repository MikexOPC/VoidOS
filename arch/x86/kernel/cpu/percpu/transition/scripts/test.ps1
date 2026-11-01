param([string]$OutputDirectory, [switch]$Installation, [switch]$Joint)
$ErrorActionPreference = 'Stop'
if ($Joint) { $Installation = $true }
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-transition-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null
$objects = @()
$rust = 'arch/x86/kernel/cpu/percpu/transition/rust/geometry/entries.rs'
$testRust = 'test/arch/x86/kernel/cpu/percpu/transition/rust/geometry/entries.rs'
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings',"$project/$testRust",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/$mode.exe" @()
}
foreach ($source in @($rust,'arch/x86/kernel/cpu/percpu/lowmemory/rust/geometry/pages.rs',
    'kernel/mm/rust/regions/validation.rs','kernel/mm/physical/rust/alignment/placement.rs')) {
    $object = "$out/" + ($source -replace '/','-') + '.obj'
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2',"$project/$source",'-o',$object)
    $objects += $object
}
$saved = Use-Msvc
try {
    $sources = @('kernel/mm/inventory.c','kernel/mm/physical/allocator.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/retention.c',
        'kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c',
        'arch/x86/kernel/cpu/percpu/lowmemory/reservation.c','arch/x86/kernel/cpu/percpu/transition/lifecycle.c',
        'arch/x86/kernel/cpu/percpu/transition/tables.c','arch/x86/kernel/cpu/percpu/transition/content/ownership.c',
        'test/arch/x86/kernel/cpu/percpu/transition/cases.c',
        'test/arch/x86/kernel/cpu/percpu/transition/faults.c')
    if ($Installation) {
        $tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
        Invoke-Checked $tool.Nasm @('-f','bin','-Wall','-Werror','-o',"$out/blob.bin",
            "$project/arch/x86/kernel/cpu/percpu/startup/assembly/trampoline/entry.asm")
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
            '-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
            "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',"$out/protocol.obj")
        $objects += "$out/protocol.obj"
        $sources = @($sources | Where-Object { $_ -ne 'test/arch/x86/kernel/cpu/percpu/transition/cases.c' }) + @(
            'arch/x86/kernel/cpu/percpu/startup/entry.c','arch/x86/kernel/cpu/percpu/startup/blob/package.c',
            'arch/x86/kernel/cpu/percpu/startup/installation/install.c',
            'test/arch/x86/kernel/cpu/percpu/startup/installation/cases.c')
    }
    if ($Joint) {
        $sources = @($sources | Where-Object { $_ -ne 'test/arch/x86/kernel/cpu/percpu/startup/installation/cases.c' }) + @(
            'arch/x86/kernel/cpu/percpu/startup/commit/commit.c',
            'test/arch/x86/kernel/cpu/percpu/startup/commit/cases.c')
    }
    foreach ($source in $sources) {
        $object = "$out/" + (($source -replace '/','-') -replace '\.c$','.obj')
        $flags = @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
            '/UNDEBUG','/c',"/I$project","$project/$source","/Fo$object","/Fd$out/host.pdb")
        if ($source -eq 'arch/x86/kernel/cpu/percpu/transition/lifecycle.c') {
            $flags += @('/Dkernel_physical_allocate_within=xt_test_allocate_within','/Dkernel_physical_pin=xt_test_pin',
                '/Dkernel_physical_release_after_unmap=xt_test_release_after_unmap','/Dks_fail_stop=xt_test_stop')
        }
        if ($source -eq 'arch/x86/kernel/cpu/percpu/startup/commit/commit.c') { $flags += '/Dks_fail_stop=xt_test_stop' }
        Invoke-Checked "$($saved.Bin)/cl.exe" $flags
        $objects += $object
    }
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $objects += @(New-PhysicalHostSync $project $out)
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") + $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    if ($Joint) {
        Invoke-Checked "$out/host.exe" @("$out/blob.bin",'positive')
        foreach ($case in @('preflight','seal-before','seal-after','verify')) {
            Invoke-Checked "$out/host.exe" @("$out/blob.bin",$case)
        }
        foreach ($fault in 1..48) { Invoke-Checked "$out/host.exe" @("$out/blob.bin",'io',"$fault") }
    } elseif ($Installation) {
        Invoke-Checked "$out/host.exe" @("$out/blob.bin")
        Invoke-Checked "$out/host.exe" @("$out/blob.bin",'freeze')
    } else {
        Invoke-Checked "$out/host.exe" @()
        Invoke-Checked "$out/host.exe" @('freeze')
    }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
"PASS: transition evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
