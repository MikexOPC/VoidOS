# ============ AP service ============

$serviceSources = @('arch/x86/kernel/cpu/percpu/service/wait.c',
    'arch/x86/kernel/cpu/percpu/service/native/runtime.c',
    'arch/x86/kernel/cpu/percpu/service/native/transport.c')
$serviceDefines = @()
if ($TestApService -ne 'none') {
    $serviceSources += 'test/arch/x86/kernel/cpu/percpu/service/integration/probe.c'
    $serviceDefines += '-DKERNEL_TEST_AP_SERVICE=1'
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
