param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-low-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null
$objects = @()
$rust = 'arch/x86/kernel/cpu/percpu/lowmemory/rust/geometry/pages.rs'
$testRust = 'test/arch/x86/kernel/cpu/percpu/lowmemory/rust/geometry/pages.rs'
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings',"$project/$testRust",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/$mode.exe" @()
}
foreach ($source in @($rust,'kernel/mm/rust/regions/validation.rs','kernel/mm/physical/rust/alignment/placement.rs')) {
    $object = "$out/" + ($source -replace '/','-') + '.obj'
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2',"$project/$source",'-o',$object)
    $objects += $object
}
$saved = Use-Msvc
try {
    $sources = @('kernel/mm/inventory.c','kernel/mm/physical/allocator.c',
        'kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c',
        'arch/x86/kernel/cpu/percpu/lowmemory/reservation.c','test/arch/x86/kernel/cpu/percpu/lowmemory/cases.c')
    foreach ($source in $sources) {
        $object = "$out/" + (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
            '/UNDEBUG','/c',"/I$project","$project/$source","/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $objects += @(New-PhysicalHostSync $project $out)
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") + $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
"PASS: low-memory evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
