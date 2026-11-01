# ============ panic coordinator, terminal entries and emergency backends ============

$panicFlags = @($flags | Where-Object { $_ -notmatch '^/O[12dx]$' }) + @('/Od')
foreach ($source in @('records.c','transport/wait.c','output/write.c','native/access.c','native/initialize.c','native/dispatch.c')) {
    $path = 'arch/x86/kernel/cpu/percpu/panic/' + $source
    $object = Join-Path $output (($path -replace '/', '-') -replace '\.c$', '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($panicFlags + @((Join-Path $project $path),('/Fo' + $object)))
    $objects += $object
}
$object = Join-Path $output 'panic-coordinator.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/panic/rust/coordinator/api.rs'),'-o',$object)
$objects += $object
$object = Join-Path $output 'panic-capture.obj'
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $object),
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/panic/assembly/entry/capture.asm'))
$objects += $object
if ($TestSmpPanic -ne 'none') {
    $object = Join-Path $output 'panic-test.obj'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'test/arch/x86/kernel/cpu/percpu/panic/integration/probe.c'),('/Fo' + $object)))
    $objects += $object
    $object = Join-Path $output 'panic-test-entry.obj'
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $object),
        (Join-Path $project 'test/arch/x86/kernel/cpu/percpu/panic/integration/assembly/fault/inject.asm'))
    $objects += $object
}
