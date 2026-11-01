# ============ BSP synchronization runtime and optional diagnostic ============

$syncSources = @('kernel/sync/lock.c','kernel/sync/acquire.c',
    'arch/x86/kernel/cpu/percpu/startup/commit/retention.c',
    'arch/x86/kernel/cpu/percpu/transition/lifecycle.c','arch/x86/kernel/cpu/percpu/transition/tables.c',
    'arch/x86/kernel/cpu/percpu/transition/native.c',
    'arch/x86/kernel/cpu/percpu/lowmemory/reservation.c','arch/x86/kernel/cpu/percpu/lowmemory/native.c',
    'arch/x86/lib/synchronization/native.c','arch/x86/lib/synchronization/context.c',
    'arch/x86/kernel/cpu/percpu/percpu.c','arch/x86/kernel/cpu/percpu/native.c','arch/x86/kernel/cpu/percpu/report.c',
    'arch/x86/kernel/cpu/percpu/clock/binding.c','arch/x86/kernel/cpu/percpu/clock/native.c',
    'arch/x86/kernel/cpu/percpu/clock/report.c',
    'arch/x86/kernel/cpu/percpu/identity/plan.c','arch/x86/kernel/cpu/percpu/identity/registry.c',
    'arch/x86/kernel/cpu/percpu/identity/native.c','arch/x86/kernel/cpu/percpu/identity/report.c',
    'arch/x86/kernel/cpu/percpu/transition/content/ownership.c',
    'arch/x86/kernel/cpu/percpu/resources/lifecycle.c','arch/x86/kernel/cpu/percpu/resources/construct.c',
    'arch/x86/kernel/cpu/percpu/resources/descriptors.c','arch/x86/kernel/cpu/percpu/resources/native/startup.c')
if ($TestKernelSync) { $syncSources += 'test/kernel/sync/integration/probe.c' }
foreach ($source in $syncSources) {
    $object = Join-Path $output (($source -replace '/', '-') -replace '\.c$', '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
    $objects += $object
}
$object = Join-Path $output 'sync-atomics.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'kernel/sync/rust/atomics/operations.rs'),'-o',$object)
$objects += $object

$object = Join-Path $output 'percpu-transition-geometry.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/transition/rust/geometry/entries.rs'),'-o',$object)
$objects += $object
$object = Join-Path $output 'sync-irq.obj'
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/c',('/Fo' + $object),
    (Join-Path $project 'arch/x86/lib/synchronization/assembly/irq/state.asm'))
$objects += $object
$object = Join-Path $output 'sync-current.obj'
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/c',('/Fo' + $object),
    (Join-Path $project 'arch/x86/lib/synchronization/assembly/context/current.asm'))
$objects += $object

$object = Join-Path $output 'percpu-gs.obj'
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/c',('/Fo' + $object),
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/assembly/gs/access.asm'))
$objects += $object

$object = Join-Path $output 'percpu-identity.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/identity/rust/validation/records.rs'),'-o',$object)
$objects += $object

$object = Join-Path $output 'percpu-ap-geometry.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/resources/rust/geometry/pages.rs'),'-o',$object)
$objects += $object

. (Join-Path $project 'arch/x86/kernel/cpu/percpu/panic/scripts/build.ps1')

$object = Join-Path $output 'percpu-low-geometry.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/cpu/percpu/lowmemory/rust/geometry/pages.rs'),'-o',$object)
$objects += $object
