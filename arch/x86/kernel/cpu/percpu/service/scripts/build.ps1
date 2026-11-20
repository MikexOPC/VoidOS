# ============ AP service ============

$serviceSources = @('arch/x86/kernel/cpu/percpu/service/wait.c',
    'arch/x86/kernel/cpu/percpu/service/native/runtime.c',
    'arch/x86/kernel/cpu/percpu/service/native/transport.c',
    'arch/x86/kernel/scheduler/remote/scope.c')
$serviceDefines = @()
if ($TestApService -ne 'none') {
    $serviceSources += 'test/arch/x86/kernel/cpu/percpu/service/integration/probe.c'
    $serviceDefines += '-DKERNEL_TEST_AP_SERVICE=1'
}
if ($TestApService -eq 'ping') { $serviceSources += 'arch/x86/mm/dma/arena.c' }
if ($TestApService -in @('admission','ap-batch','ap-batch-rollback','ap-preempt','ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete','ap-console','ap-console-incomplete','ap-console-ipi','ap-console-ipi-no-ack','ap-console-ipi-inflight')) {
    $serviceDefines += '-DKERNEL_AP_ADMISSION=1'
    $serviceSources += 'arch/x86/kernel/scheduler/admission/native.c'
    if ($TestApService -eq 'admission') { $serviceSources += 'test/arch/x86/kernel/scheduler/admission/probe.c' }
    else { $serviceSources += @('arch/x86/kernel/scheduler/batch/validation.c',
        'arch/x86/kernel/scheduler/batch/runtime.c','arch/x86/kernel/scheduler/batch/work.c',
        'test/arch/x86/kernel/scheduler/batch/probe.c') }
}
if ($ApTasks) {
    $serviceDefines += '-DKERNEL_AP_ADMISSION=1'
    $serviceSources += @('arch/x86/kernel/scheduler/admission/native.c','arch/x86/kernel/scheduler/remote/remote.c',
        'arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.c',
        'arch/x86/kernel/drivers/video/framebuffer/console/wake/hardware.c',
        'arch/x86/kernel/drivers/input/ps2/shell/remote.c')
}
if ($TestApService -in @('ap-preempt','ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete')) {
    $serviceSources += 'arch/x86/kernel/scheduler/batch/preempt.c'
    $serviceDefines += '-DKERNEL_TEST_AP_PREEMPTION=1'
}
if ($TestApService -in @('ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete')) {
    $serviceSources += 'arch/x86/kernel/scheduler/batch/concurrent.c'
    $serviceDefines += '-DKERNEL_TEST_AP_CONCURRENT=1'
}
if ($TestApService -like 'ap-console*') {
    $serviceSources += @('kernel/console/video/requests/queue.c','test/kernel/console/video/ap/channel.c')
    $notifyCfg = @()
    if ($TestApService -like 'ap-console-ipi*') {
        $notifyCfg = @('--cfg','console_ap_notify')
        $serviceSources += @('arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.c',
            'arch/x86/kernel/drivers/video/framebuffer/console/wake/hardware.c')
    }
    Invoke-Compiler $rustc (@('--edition=2024','--crate-type=staticlib','--cfg','console_ap_diagnostic',
        '--target','x86_64-pc-windows-msvc','-C','panic=abort','-C','opt-level=2',
        '-C','lto=fat','-C','codegen-units=1','-C','overflow-checks=yes','-D','warnings',
        "$project/kernel/console/video/rust/ffi/requests.rs",'-o',"$output/ap-console.lib") + $notifyCfg)
    $objects += "$output/ap-console.lib"
}
foreach ($source in $serviceSources) {
    $object = Join-Path $output (($source -replace '/', '-') -replace '\.c$', '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
    $objects += $object
}
$object = Join-Path $output 'ipi-mailbox.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
    "$project/arch/x86/kernel/cpu/percpu/service/rust/mailbox/api.rs",'-o',$object)
$objects += $object
foreach ($part in @('runtime/idle')) {
    $object = Join-Path $output ('ipi-' + ($part -replace '/', '-') + '.obj')
    Invoke-Compiler $tool.Nasm (@('-f','win64','-Wall','-Werror') + $serviceDefines + @('-o',$object,
        "$project/arch/x86/kernel/cpu/percpu/service/assembly/$part.asm"))
    $objects += $object
}
