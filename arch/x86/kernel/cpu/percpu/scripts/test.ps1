param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/percpu-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual provider and modeled CPU boundary ============

$objects = @()
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/kernel/sync/rust/atomics/operations.rs",'-o',"$out/atomics.obj")
$objects += "$out/atomics.obj"
$saved = Use-Msvc
try {
    $sources = @('kernel/sync/lock.c','kernel/sync/acquire.c','kernel/time/clock.c',
        'arch/x86/lib/synchronization/context.c','arch/x86/kernel/cpu/percpu/percpu.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/retention.c',
        'test/arch/x86/kernel/cpu/percpu/platform.c','test/arch/x86/kernel/cpu/percpu/cases.c') |
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
        foreach ($mode in @('if1','if0','bad-base','bad-self','exhausted')) { Invoke-Checked "$out/host.exe" @($mode) }
    }
    Invoke-Cppcheck $project $out $sources
} finally { Restore-Msvc $saved }
"PASS: per-CPU host evidence $out"
