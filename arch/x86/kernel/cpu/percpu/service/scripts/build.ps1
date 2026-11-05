# ============ AP service ============

$serviceSources = @('arch/x86/kernel/cpu/percpu/service/wait.c',
    'arch/x86/kernel/cpu/percpu/service/native/runtime.c',
    'arch/x86/kernel/cpu/percpu/service/native/transport.c')
$serviceDefines = @()
if ($TestApService -ne 'none') {
    $serviceSources += 'test/arch/x86/kernel/cpu/percpu/service/integration/probe.c'
    $serviceDefines += '-DKERNEL_TEST_AP_SERVICE=1'
}
if ($TestApService -eq 'ping') { $serviceSources += 'arch/x86/mm/dma/arena.c' }
if ($TestApService -in @('admission','ap-batch','ap-batch-rollback')) {
    $serviceDefines += '-DKERNEL_TEST_AP_ADMISSION=1'
    $serviceSources += 'arch/x86/kernel/scheduler/admission/native.c'
    if ($TestApService -eq 'admission') { $serviceSources += 'test/arch/x86/kernel/scheduler/admission/probe.c' }
    else { $serviceSources += @('arch/x86/kernel/scheduler/batch/validation.c',
        'arch/x86/kernel/scheduler/batch/runtime.c','arch/x86/kernel/scheduler/batch/work.c',
        'test/arch/x86/kernel/scheduler/batch/probe.c') }
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
foreach ($part in @('entry/interrupt','runtime/idle')) {
    $object = Join-Path $output ('ipi-' + ($part -replace '/', '-') + '.obj')
    Invoke-Compiler $tool.Nasm (@('-f','win64','-Wall','-Werror') + $serviceDefines + @('-o',$object,
        "$project/arch/x86/kernel/cpu/percpu/service/assembly/$part.asm"))
    $objects += $object
}
