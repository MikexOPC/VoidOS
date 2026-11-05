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
Invoke-Checked rustc @('--edition=2024','--test','-D','warnings','-C','opt-level=2',
    '-C','overflow-checks=yes',$api,'-o',"$out/state-release.exe")
Invoke-Checked "$out/state-release.exe" @('--test-threads=1')
Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','--emit=metadata',
    '-D','warnings',$api,'-o',"$out/mailbox-clippy.rmeta")
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
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/UNDEBUG',"/I$project","$project/test/arch/x86/kernel/scheduler/admission/ffi_test.c",
        "/Fe$out/admission.exe","/Fo$out/admission.obj","/Fd$out/admission.pdb",'/link',
        "$out/mailbox.obj",'/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/admission.exe" @()
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
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',
        "$project/arch/x86/kernel/interrupts/return/rust/frame/validation.rs",'-o',"$out/irq-frame.obj")
    $nativeTest = "$project/test/arch/x86/kernel/cpu/percpu/service/native_test.c"
    $admissionNative = "$project/arch/x86/kernel/scheduler/admission/native.c"
    $admissionLocal = "$project/arch/x86/kernel/scheduler/integration/local.c"
    foreach ($part in @(@($native, 'native'), @($nativeTest, 'native-test'),
        @($admissionNative,'admission-native'), @($admissionLocal,'admission-local'))) {
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/DKERNEL_TEST_AP_SERVICE=1','/c',"/I$project",$part[0],"/Fo$out/$($part[1]).obj","/Fd$out/native.pdb")
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/MD','/fsanitize=address',"/Fe$out/native.exe",
        "$out/native.obj","$out/native-test.obj","$out/admission-native.obj","$out/admission-local.obj",
        "$out/mailbox.obj","$out/irq-frame.obj",'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/native.exe" @()
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/UNDEBUG',"/I$project","$project/arch/x86/kernel/scheduler/batch/validation.c",
        "$project/arch/x86/kernel/scheduler/integration/local.c","$project/arch/x86/kernel/scheduler/capabilities.c",
        "$project/test/arch/x86/kernel/scheduler/batch/validation_test.c",
        "/Fo$out/","/Fe$out/ap-gate.exe","/Fd$out/ap-gate.pdb",'/link',
        "$out/mailbox.obj","$out/irq-frame.obj",'/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/ap-gate.exe" @()
    Invoke-Cppcheck $project $out @($source, $wait, $test, $native, $nativeTest,
        $admissionNative, $admissionLocal, "$project/test/arch/x86/kernel/scheduler/admission/probe.c",
        "$project/arch/x86/kernel/scheduler/batch/validation.c","$project/arch/x86/kernel/scheduler/batch/runtime.c",
        "$project/arch/x86/kernel/scheduler/batch/work.c","$project/test/arch/x86/kernel/scheduler/batch/probe.c",
        "$project/test/arch/x86/kernel/scheduler/batch/validation_test.c",
        "$project/test/arch/x86/kernel/scheduler/admission/ffi_test.c",
        "$project/arch/x86/kernel/cpu/percpu/service/native/transport.c",
        "$project/arch/x86/kernel/interrupts/apic/icr/icr.c",
        "$project/test/arch/x86/kernel/cpu/percpu/service/integration/probe.c")
} finally { Restore-Msvc $saved }
"PASS: IPI mailbox host evidence $out"
$python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe"
Invoke-Checked $python @('-B',"$project/test/arch/x86/kernel/cpu/percpu/service/integration/firmware_test.py")
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
