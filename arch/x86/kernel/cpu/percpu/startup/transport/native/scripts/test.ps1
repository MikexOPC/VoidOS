param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) {[IO.Path]::GetFullPath($OutputDirectory)} else {
    Join-Path $project ('kernel/build/ap-native-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual controller and coordinator, modeled device ============

Invoke-OsChecked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2',"$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',"$out/protocol.obj") -Log "$out/tools.log"
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/startup/transport/native/run.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/wake.c','arch/x86/kernel/interrupts/apic/icr/icr.c','kernel/time/clock.c',
        'test/arch/x86/kernel/cpu/percpu/startup/transport/native/cases.c')
    $objects = @("$out/protocol.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/Zi',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source","/Fo$object","/Fd$out/test.pdb") -Log "$out/tools.log"
        $objects += $object
    }
    Invoke-OsChecked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe")+
        $objects+@('/link','/incremental:no','/DEBUG','/WX')) -Log "$out/tools.log"
    foreach ($case in @('positive','late-ready','no-nmi','wake-read','wake-write','nmi-write','reject','bad-online','stopped-clock')) {
        Invoke-OsChecked "$out/host.exe" @($case) -Log "$out/tools.log"
    }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object {"$project/$_"})
} finally {Restore-Msvc $saved}
& (Join-Path $PSScriptRoot 'rearm.ps1') -OutputDirectory "$out/rearm"
$python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe"
Invoke-OsChecked $python @('-B',"$project/test/arch/x86/kernel/cpu/percpu/startup/transport/registers_test.py") -Log "$out/registers.log"
"PASS: native AP controller host evidence $out"
if ($MyInvocation.InvocationName -ne '.') {exit 0}
