param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/percpu-identity-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual provider and modeled CPU boundary ============

$objects = @()
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/kernel/sync/rust/atomics/operations.rs",'-o',"$out/atomics.obj")
$objects += "$out/atomics.obj"
$rules = "$project/arch/x86/kernel/cpu/percpu/identity/rust/validation/records.rs"
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',$rules,'-o',"$out/identity.obj")
$objects += "$out/identity.obj"
foreach ($mode in @('debug','release')) {
    $options = @('--edition=2024','--test','-D','warnings',$rules,'-o',"$out/rust-$mode.exe")
    if ($mode -eq 'release') { $options += @('-C','opt-level=2') }
    Invoke-Checked rustc $options
    Invoke-Checked "$out/rust-$mode.exe" @()
}
$saved = Use-Msvc
try {
    $sources = @('kernel/sync/lock.c','kernel/sync/acquire.c','kernel/time/clock.c',
        'arch/x86/lib/synchronization/context.c','arch/x86/kernel/cpu/percpu/percpu.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/retention.c',
        'test/arch/x86/kernel/cpu/percpu/platform.c','test/arch/x86/kernel/cpu/percpu/identity/cases.c',
        'arch/x86/kernel/cpu/percpu/identity/plan.c','arch/x86/kernel/cpu/percpu/identity/registry.c') |
        ForEach-Object { Join-Path $project $_ }
    foreach ($source in $sources) {
        $object = "$out/" + (($source.Substring($project.Length + 1) -replace '[\\/]','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$source,"/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    foreach ($repetition in 1..10) {
        foreach ($mode in @('if1','if0')) { Invoke-Checked "$out/host.exe" @($mode) }
    }
    Invoke-Cppcheck $project $out $sources
} finally { Restore-Msvc $saved }
"PASS: per-CPU identity host evidence $out"
