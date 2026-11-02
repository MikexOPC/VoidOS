param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ipi-service-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null
$api = "$project/arch/x86/kernel/cpu/percpu/service/rust/mailbox/api.rs"

# ============ Rust state machine and real concurrent atomics ============

Invoke-Checked rustc @('--edition=2024','--test','-D','warnings',$api,'-o',"$out/state.exe")
Invoke-Checked "$out/state.exe" @('--test-threads=1')
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',$api,'-o',"$out/mailbox.obj")

# ============ actual C FFI and ASan host consumers ============

$saved = Use-Msvc
try {
    $source = "$project/test/arch/x86/kernel/cpu/percpu/service/mailbox_test.c"
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/UNDEBUG',"/I$project",$source,"/Fe$out/ffi.exe",
        "/Fo$out/ffi.obj","/Fd$out/ffi.pdb",'/link',"$out/mailbox.obj",'/incremental:no','/DEBUG','/WX')
    foreach ($run in 1..20) { Invoke-Checked "$out/ffi.exe" @() }
    $wait = "$project/arch/x86/kernel/cpu/percpu/service/wait.c"
    $test = "$project/test/arch/x86/kernel/cpu/percpu/service/wait_test.c"
    foreach ($part in @(@($wait, 'wait'), @($test, 'wait-test'))) {
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$part[0],"/Fo$out/$($part[1]).obj","/Fd$out/wait.pdb")
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/MD','/fsanitize=address',"/Fe$out/wait.exe",
        "$out/wait.obj","$out/wait-test.obj","$out/mailbox.obj",'/link','/incremental:no','/DEBUG','/WX')
    foreach ($case in @('ack','clock-error','stopped','backwards','mask','wrap','wrap32','timeout','invalid')) {
        Invoke-Checked "$out/wait.exe" @($case)
    }
    $native = "$project/arch/x86/kernel/cpu/percpu/service/native/runtime.c"
    $nativeTest = "$project/test/arch/x86/kernel/cpu/percpu/service/native_test.c"
    foreach ($part in @(@($native, 'native'), @($nativeTest, 'native-test'))) {
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$part[0],"/Fo$out/$($part[1]).obj","/Fd$out/native.pdb")
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/MD','/fsanitize=address',"/Fe$out/native.exe",
        "$out/native.obj","$out/native-test.obj","$out/mailbox.obj",'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/native.exe" @()
    Invoke-Cppcheck $project $out @($source, $wait, $test, $native, $nativeTest,
        "$project/arch/x86/kernel/cpu/percpu/service/native/transport.c",
        "$project/test/arch/x86/kernel/cpu/percpu/service/integration/probe.c")
} finally { Restore-Msvc $saved }
"PASS: IPI mailbox host evidence $out"
$python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe"
Invoke-Checked $python @('-B',"$project/test/arch/x86/kernel/cpu/percpu/service/integration/firmware_test.py")
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
