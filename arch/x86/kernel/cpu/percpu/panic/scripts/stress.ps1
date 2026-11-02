param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/panic-stress-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ real Rust atomics and C/ASan diagnostic rendezvous ============

Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',
    "$project/kernel/sync/rust/atomics/operations.rs",'-o',"$out/atomics.obj")
$saved = Use-Msvc
try {
    $sources = @('test/arch/x86/kernel/cpu/percpu/panic/concurrency/barrier.c',
        'test/arch/x86/kernel/cpu/percpu/panic/concurrency/barrier_test.c')
    $objects = @("$out/atomics.obj")
    foreach ($source in $sources) {
        $object = "$out/" + (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project","$project/$source","/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") + $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    foreach ($run in 1..20) { Invoke-Checked "$out/host.exe" @('ok') }
    foreach ($case in @('invalid','duplicate','clock','stalled')) { Invoke-Checked "$out/host.exe" @($case) }
    Invoke-Cppcheck $project $out ($sources | ForEach-Object {"$project/$_"})
} finally { Restore-Msvc $saved }
"PASS: panic stress host evidence $out"
$python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe"
Invoke-Checked $python @('-B',"$project/test/arch/x86/kernel/cpu/percpu/panic/concurrency/checkpoints_test.py")
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
