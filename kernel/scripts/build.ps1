param([ValidateSet('none','ping','missing-ack','stale-ack','panic-idle','panic-handler','release-missing-ack','ap-timer','k5-ap','admission','ap-batch','ap-batch-rollback','ap-preempt','ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete','ap-console','ap-console-incomplete','ap-console-ipi','ap-console-ipi-no-ack','ap-console-ipi-inflight')][string]$TestApService = 'none', [ValidateSet('none','withhold-ack','lock-held','contended')][string]$TestApStress = 'none', [ValidateSet('none','bsp','ap')][string]$TestApGroupPanic = 'none', [switch]$TestApLaunch, [switch]$TestApNoNmi, [switch]$TestApRemotePanic, [switch]$TestApSequence, [switch]$TestApSequenceFault,
    [switch]$TestSchedulerPeriodic, [switch]$TestSchedulerPreemption, [ValidateSet('none','guard-low','guard-high','nx','retired','rollback','blocked','irq-timeout','idle-timeout','idle-repeat-timeout')][string]$TestSchedulerFault = 'none',
    [ValidateSet('none','guard','nx')][string]$TestApResourceFault = 'none',
    [ValidateSet('none','prepare','voluntary','opcode','page','nmi','double','no-simd','recursive','lock-held','prepare-rollback','no-simd-call')][string]$TestSmpPanic = 'none',
    [ValidateSet('none','prepare','rollback','freeze','image','image-rollback','joint','joint-fail-before','joint-fail-after','joint-panic')][string]$TestApTransition = 'none',
    [switch]$TestApLowAbsent, [switch]$TestApCommit, [switch]$TestKernelSync, [ValidateSet('none','wait','irq-enabled')][string]$TestKernelSyncFault = 'none',
    [switch]$TestLoaderRetired, [string]$ToolchainBin, [switch]$TestBootstrapMemory, [switch]$TestPs2Decoder, [switch]$TestPs2Controller, [switch]$Ps2Input, [switch]$Ps2Mouse, [switch]$TestPanic, [string]$OutputDirectory, [switch]$Install, [switch]$EventLoop,
    [switch]$TestWatchdogLostLapic, [switch]$TestFramebufferMapping, [switch]$TestConsoleActive, [switch]$TestConsoleDraw,
    [switch]$TestWatchdogAlternate, [switch]$TestConsoleBatch, [switch]$ConsoleShell, [switch]$InteractiveInput,
    [switch]$TestWatchdogCleanupRecovery, [switch]$TestSchedulerPreservation, [switch]$TestIommuActivation, [switch]$TestIommuDma, [switch]$TestVgpuMapping, [switch]$TestVgpuInitialization, [switch]$TestVgpuQueues, [switch]$TestVgpuResources, [switch]$TestVgpuResourceReject, [switch]$TestVgpuScanout, [switch]$TestVgpuIommu, [switch]$TestVgpuIommuFault, [switch]$VgpuIommu, [switch]$TestVgpuPresentFault, [switch]$TestVgpuPresentStall, [switch]$TestKernelHeap, [switch]$TestPagingTamper, [switch]$ProfileVgpuFlush, [switch]$ProfileVgpuNoPresent, [switch]$TestIommuDiscovery, [switch]$TestIommuRegisters, [switch]$TestIommuDomain, [switch]$TestIommuQi, [switch]$TestPciEcam, [switch]$TestPciEcamAbsent, [switch]$TestPciTopology, [switch]$TestIommuRouting,
    [ValidateSet('none','execute','unmapped','guard-low','guard-high')][string]$TestWindowFault = 'none',
    [ValidateSet('none','opcode','selector','divide','stack','nested','double')][string]$TestCpuFault = 'none',
    [ValidateSet('none','retired','guard-high','execute','return')][string]$TestMainFault = 'none',
    [ValidateSet('none','retired','guard-low','guard-high','execute')][string]$TestRegionFault = 'none',
    [switch]$PlanOnly, [switch]$UseRustPlan, [switch]$TestK5Privilege, [switch]$TestK5Syscall, [switch]$TestK5Copy, [int]$TestK5IrqNegative = 0, [switch]$TestK6Process, [int]$TestK6Negative = 0, [switch]$TestShellBridgeFault)
. (Join-Path $PSScriptRoot '../../scripts/lib/layout.ps1')
if (!$PlanOnly) { Assert-LayoutCopies }
$ErrorActionPreference = 'Stop'
$boundBuildOptions = @($PSBoundParameters.Keys | Where-Object { $_ -notin @('PlanOnly','UseRustPlan') })
if ($TestK6Process -and
    @($boundBuildOptions | Where-Object { $_ -notin @('TestK6Process','TestK6Negative','OutputDirectory','ToolchainBin') }).Count) {
    throw 'K6 process requires an isolated diagnostic profile.'
}
if (($TestK5Syscall -or $TestK5Copy) -and ($PlanOnly -or $UseRustPlan -or
    @($boundBuildOptions | Where-Object { $_ -notin @('TestK5Syscall','TestK5Copy','TestK5IrqNegative','OutputDirectory','ToolchainBin') }).Count)) {
    throw 'K5 syscall requires an isolated diagnostic profile.'
}
if ($TestK5Privilege -and ($PlanOnly -or $UseRustPlan -or
    @($boundBuildOptions | Where-Object { $_ -notin @('TestK5Privilege','TestK5IrqNegative','OutputDirectory','ToolchainBin') }).Count)) {
    throw 'K5 privilege requires an isolated diagnostic profile.'
}
. (Join-Path $PSScriptRoot 'build/profiles.ps1')
if ($UseRustPlan) {
    if ($PlanOnly -or !$OutputDirectory -or $TestApService -notin @('none','ap-timer') -or
        @($boundBuildOptions | Where-Object { $_ -notin @('TestSchedulerPeriodic','TestApService','TestK6Process','TestK6Negative','OutputDirectory','ToolchainBin') }).Count) {
        throw 'Rust build plan requires a supported profile and a new OutputDirectory.'
    }
    $OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
    if (Test-Path -LiteralPath $OutputDirectory) { throw 'Rust build plan output already exists.' }
}
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if ($PlanOnly) {
    foreach ($key in $boundBuildOptions) {
        if ($key -notin @('TestSchedulerPeriodic','TestApService','TestK6Process','TestK6Negative','Ps2Input','Ps2Mouse','ConsoleShell','InteractiveInput')) { throw 'Unsupported plan-only options.' }
    }
    if ($TestApService -notin @('none','ap-timer')) { throw 'Unsupported plan-only options.' }
    . (Join-Path $PSScriptRoot 'build/plan-only.ps1')
} else {
    . (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
}
if (!$PlanOnly -and !$ToolchainBin) {
    $version = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin/Hostx64/x64'
}
if (!$PlanOnly -and !$UseRustPlan) {
    $output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $project 'kernel/build' }
    New-Item -ItemType Directory -Force -Path $output | Out-Null
}

# ====== ogni compilazione deve terminare senza errori ===== #

function Invoke-Compiler([string]$program, [string[]]$arguments) {
    if ($PlanOnly) { Write-PlanCommand $program $arguments; return }
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita: $program" }
}

# ====== Assert-ApTemplateSize ======

function Assert-ApTemplateSize([string]$Path) {
    if ((Get-Item -LiteralPath $Path).Length -ne 4096) { throw 'Invalid AP template size' }
}

if (!$PlanOnly) {
    Write-BootSection 'kernel autonomo C e Rust'
    $sdk = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stddef.h') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (!$sdk) { throw 'Header UCRT del Windows SDK assenti.' }
}
if ($UseRustPlan) {
    . (Join-Path $PSScriptRoot 'build/rust-plan.ps1')
    return
}
. (Join-Path $PSScriptRoot 'build/profiles/flags.ps1')
$flags += '/DKERNEL_PANIC_RUNTIME=1'
if ($TestK5Privilege) { $flags += '/DKERNEL_TEST_K5_PRIVILEGE=1' }
if ($TestApService -eq 'k5-ap') { $flags += '/DKERNEL_TEST_K5_AP=1' }
if ($TestK5Syscall) { $flags += '/DKERNEL_TEST_K5_SYSCALL=1' }
if ($TestK5Copy) { $flags += '/DKERNEL_TEST_K5_COPY=1' }
if ($TestK6Process) { $flags += '/DKERNEL_TEST_K6_PROCESS=1' }
if ($TestK6Negative) {
    if (!$TestK6Process -or $TestK6Negative -notin 31..37) { throw 'Invalid K6 negative case.' }
    $flags += "/DKERNEL_K6_NEGATIVE=$TestK6Negative"
}
if ($TestK5IrqNegative) {
    if (($TestK5Copy -and $TestK5IrqNegative -notin 20..25) -or ($TestK5Syscall -and $TestK5IrqNegative -notin 10..18) -or ($TestK5Privilege -and $TestK5IrqNegative -notin 1..5) -or ($TestApService -eq 'k5-ap' -and $TestK5IrqNegative -notin 6..9) -or
        (!$TestK5Privilege -and !$TestK5Syscall -and !$TestK5Copy -and $TestApService -ne 'k5-ap')) { throw 'Invalid K5 IRQ negative case.' }
    $flags += "/DKERNEL_K5_IRQ_NEGATIVE=$TestK5IrqNegative"
}
if ($TestApTransition -ne 'none') {
    if ($TestBootstrapMemory -or $TestApLowAbsent -or $TestSmpPanic -ne 'none') { throw 'Transition diagnostic requires ordinary early reservation and owns the terminal checkpoint.' }
    if ($TestApTransition -like 'joint*' -and $TestApCommit) { throw 'Joint diagnostic owns all resource commits.' }
    $flags += ('/DKERNEL_TEST_AP_TRANSITION=' + $(switch ($TestApTransition) { 'prepare' {1} 'rollback' {2} 'freeze' {3} 'image' {4} 'image-rollback' {5} 'joint' {6} 'joint-fail-before' {7} 'joint-fail-after' {8} 'joint-panic' {9} }))
    if ($TestApTransition -in @('image','image-rollback','joint','joint-fail-before','joint-fail-after','joint-panic')) { $flags += '/DKERNEL_TEST_AP_IMAGE=1' }
}
if ($TestApLowAbsent) { $flags += '/DKERNEL_TEST_AP_LOW_ABSENT=1' }
if ($TestSmpPanic -ne 'none') {
    if ($TestApCommit) { throw 'TestSmpPanic owns the commit; do not combine with TestApCommit.' }
    $panicMode = switch ($TestSmpPanic) { 'prepare' {1} 'voluntary' {2} 'opcode' {3} 'page' {4} 'nmi' {5} 'double' {6} 'no-simd' {7} 'recursive' {8} 'lock-held' {9} 'prepare-rollback' {10} 'no-simd-call' {11} }
    $flags += ('/DKERNEL_TEST_SMP_PANIC=' + $panicMode)
}
if ($TestApCommit) { $flags += '/DKERNEL_TEST_AP_COMMIT=1' }
$objects = @()
if ($TestCpuFault -ne 'none') {
    $mode = switch ($TestCpuFault) { 'opcode' {1} 'selector' {2} 'divide' {3} 'stack' {4} 'nested' {1} 'double' {2} }
    $flags += ('/DKERNEL_TEST_CPU_FAULT=' + $mode)
    if ($TestCpuFault -eq 'nested') { $flags += '/DKERNEL_TEST_NESTED_FAULT=1' }
    if ($TestCpuFault -eq 'double') { $flags += '/DKERNEL_TEST_DOUBLE_FAULT=1' }
}
# Test code is linked only into the diagnostic profiles that call it (B6); the production image
# must not contain it. fault.c has code only for faults 1-4, drawing/probe.c only with the mapping probe.
$diagnosticSources = @()
if ($TestIommuDiscovery) { $diagnosticSources+=@('arch/x86/kernel/drivers/memory/iommu/intel/discovery/table.c',
    'arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.c','test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/probe.c') }
if ($TestPciEcam) { $diagnosticSources+=@('arch/x86/kernel/drivers/bus/pci/ecam/table.c','arch/x86/kernel/drivers/bus/pci/ecam/find.c','arch/x86/kernel/drivers/bus/pci/ecam/reader.c','arch/x86/kernel/drivers/bus/pci/ecam/validation.c','arch/x86/kernel/drivers/bus/pci/ecam/backend/access.c','test/arch/x86/kernel/drivers/bus/pci/ecam/integration/probe.c') }
if ($TestSchedulerFault -in @('guard-low','guard-high','nx','retired')) { $diagnosticSources += 'test/arch/x86/kernel/scheduler/fault/fault.c' }
if ($TestFramebufferMapping) { $diagnosticSources += 'test/arch/x86/kernel/drivers/video/framebuffer/drawing/probe.c' }
if ($TestVgpuMapping) { $diagnosticSources += 'test/arch/x86/kernel/drivers/video/virtio/mapping/probe.c' }
if ($TestVgpuInitialization) {
    $diagnosticSources += @('test/arch/x86/kernel/drivers/video/virtio/initialization/probe.c',
        'arch/x86/kernel/drivers/video/virtio/initialization/transport.c',
        'arch/x86/kernel/drivers/video/virtio/initialization/access.c',
        'kernel/drivers/video/virtio/initialization/protocol.c')
}
foreach ($source in (@('arch/x86/kernel/scheduler/context.c','arch/x86/kernel/scheduler/capabilities.c',
    'arch/x86/kernel/scheduler/stacks.c','arch/x86/kernel/scheduler/mapping/native.c',
    'arch/x86/kernel/scheduler/mapping/access.c',
    'arch/x86/kernel/scheduler/integration/kernel.c','arch/x86/kernel/scheduler/integration/local.c','arch/x86/kernel/scheduler/integration/lock.c','arch/x86/kernel/scheduler/integration/workload.c',
    'arch/x86/kernel/scheduler/integration/report.c','arch/x86/kernel/scheduler/integration/validation.c','arch/x86/kernel/scheduler/idle/idle.c',
    'arch/x86/kernel/scheduler/persistent/workload.c',
    'arch/x86/kernel/scheduler/dispatcher/engine.c','arch/x86/kernel/scheduler/diagnostic/profiles.c',
    'arch/x86/kernel/scheduler/irq/bridge.c','arch/x86/kernel/scheduler/runtime/memory.c','arch/x86/kernel/scheduler/runtime/payload.c','arch/x86/kernel/entry.c','arch/x86/kernel/serial.c','arch/x86/mm/window/page.c','arch/x86/mm/window/access.c','kernel/main.c','kernel/startup/early.c','kernel/startup/platform.c','kernel/startup/devices.c','kernel/startup/profile.c',
    'kernel/bootstrap/memory.c','kernel/boot/contract.c','kernel/drivers/video/framebuffer/handoff.c','kernel/drivers/video/framebuffer/mapping.c',
    'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
    'kernel/drivers/video/virtio/mapping/plan.c','arch/x86/mm/paging/iommu/iommu_registry.c','arch/x86/mm/paging/surface/registry.c','arch/x86/mm/paging/dma/dma_registry.c','arch/x86/mm/paging/heap/heap_registry.c','arch/x86/kernel/cpu/percpu/resources/paging/registry.c','arch/x86/mm/heap/binding.c','kernel/mm/heap/heap.c','kernel/mm/heap/core/state.c','kernel/mm/heap/core/reserve.c','kernel/mm/heap/core/release.c','kernel/mm/heap/core/guard.c','kernel/mm/heap/core/fill.c','lib/containers/bitmap.c','kernel/time/clock.c','kernel/mm/physical/holdings.c','arch/x86/mm/paging/gpu/authority.c','arch/x86/mm/paging/gpu/validation.c',
    'kernel/drivers/video/virtio/discovery/configuration/read.c',
    'arch/x86/kernel/drivers/video/virtio/preflight.c','arch/x86/kernel/drivers/video/virtio/construction.c',
    'arch/x86/kernel/drivers/video/virtio/lifecycle.c','arch/x86/kernel/drivers/video/virtio/pci/access.c',
    'arch/x86/kernel/drivers/video/virtio/integration/kernel.c',
    'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c','kernel/mm/objects/pool.c','kernel/diagnostics/log.c',
    'arch/x86/mm/regions/mapping.c','arch/x86/mm/regions/access.c',
    'arch/x86/mm/arena/objects.c',
    'arch/x86/lib/processor/processor_access.c','arch/x86/mm/paging/retirement/retire.c','arch/x86/mm/paging/retirement/policy.c','arch/x86/mm/paging/retirement/native.c',
    'arch/x86/mm/paging/inventory.c','arch/x86/mm/paging/access.c',
    'arch/x86/mm/paging/authorization/validation.c',
    'arch/x86/kernel/drivers/video/framebuffer/authority.c',
    'arch/x86/kernel/drivers/video/framebuffer/preflight.c',
    'arch/x86/kernel/drivers/video/framebuffer/construction.c',
    'arch/x86/kernel/drivers/video/framebuffer/lifecycle.c',
    'arch/x86/kernel/drivers/video/framebuffer/native/access.c',
    'arch/x86/kernel/drivers/video/framebuffer/pixels/write.c','arch/x86/kernel/drivers/video/framebuffer/pixels/transaction.c',
    'arch/x86/kernel/drivers/video/framebuffer/pixels/native.c',
    'arch/x86/kernel/drivers/video/framebuffer/drawing/draw.c','arch/x86/kernel/drivers/video/framebuffer/drawing/cursor.c','kernel/console/video/pointer/shape.c',
    'arch/x86/kernel/drivers/video/framebuffer/gateway/cell.c',
    'arch/x86/kernel/drivers/video/framebuffer/gateway/inventory.c',
    'arch/x86/kernel/drivers/video/framebuffer/gateway/native.c',
    'arch/x86/kernel/drivers/video/framebuffer/font/font.c',
    'arch/x86/kernel/drivers/video/framebuffer/integration/kernel.c',
    'lib/firmware/acpi/rsdp.c',
    'kernel/firmware/acpi/root.c',
    'kernel/firmware/acpi/madt/parser.c','kernel/firmware/acpi/madt/records.c',
    'arch/x86/kernel/interrupts/apic/profile.c',
    'arch/x86/kernel/interrupts/apic/probe.c','arch/x86/kernel/interrupts/apic/access.c','arch/x86/kernel/interrupts/apic/icr/icr.c',
    'arch/x86/kernel/interrupts/apic/control/masked.c','arch/x86/kernel/interrupts/apic/control/access.c',
    'arch/x86/kernel/interrupts/return/table.c','arch/x86/kernel/interrupts/registry/registry.c',
    'arch/x86/kernel/serial/interrupts/driver.c','arch/x86/kernel/serial/transmit/buffer.c','arch/x86/kernel/serial/queue/queue.c',
    'arch/x86/kernel/timer/lapic/calibration.c','arch/x86/kernel/timer/lapic/driver.c','arch/x86/kernel/timer/lapic/access.c',
    'arch/x86/kernel/timer/lapic/runtime/runtime.c',
    'arch/x86/kernel/timer/hpet/description/table.c','arch/x86/kernel/timer/hpet/description/scan.c',
    'arch/x86/kernel/timer/hpet/description/integration/kernel.c',
    'arch/x86/kernel/timer/hpet/description/probe/policy.c','arch/x86/kernel/timer/hpet/description/probe/hardware.c',
    'arch/x86/kernel/timer/hpet/watchdog/core.c','arch/x86/kernel/timer/hpet/watchdog/device.c',
    'arch/x86/kernel/timer/hpet/watchdog/route.c','arch/x86/kernel/timer/hpet/watchdog/wait.c',
    'arch/x86/kernel/timer/hpet/watchdog/accounting/budget.c',
    'arch/x86/kernel/timer/hpet/watchdog/hardware/access.c',
    'arch/x86/kernel/timer/hpet/watchdog/compatibility/select.c',
    'arch/x86/kernel/timer/hpet/watchdog/recovery/control.c',
    'arch/x86/kernel/timer/hpet/watchdog/integration/kernel.c',
    'arch/x86/mm/devices/access.c','arch/x86/mm/devices/lapic.c',
    'arch/x86/kernel/exceptions/terminal.c','arch/x86/kernel/descriptors/stacks.c','arch/x86/kernel/descriptors/segments.c','arch/x86/kernel/descriptors/protection.c',
    'arch/x86/kernel/descriptors/stack_access.c','arch/x86/kernel/stack/main.c','arch/x86/lib/descriptors/gdt.c','arch/x86/lib/descriptors/tss.c','lib/memory/address_ranges.c','lib/geometry/rect.c','lib/geometry/damage.c') + $diagnosticSources |
    Where-Object { !$Ps2Input -or $_ -notin @('arch/x86/kernel/scheduler/integration/workload.c',
        'arch/x86/kernel/scheduler/idle/idle.c','arch/x86/kernel/scheduler/persistent/workload.c') })) {
    $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
    $objects += $object
}
if ($bootstrapDiagnostics) {
    $object = Join-Path $output 'bootstrap-probes.obj'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'test/kernel/bootstrap/probes.c'),('/Fo' + $object)))
    $objects += $object
}
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/io.obj'),
    (Join-Path $project 'arch/x86/kernel/assembly/platform/io.asm'))
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/exception-entry.obj'),
    (Join-Path $project 'arch/x86/kernel/exceptions/assembly/entry/terminal.asm'))
$objects += ($output + '/exception-entry.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/segment-entry.obj'),
    (Join-Path $project 'arch/x86/kernel/descriptors/assembly/activation/segments.asm'))
$objects += ($output + '/segment-entry.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/user-protection-entry.obj'),
    (Join-Path $project 'arch/x86/kernel/descriptors/assembly/protection/apply.asm'))
$objects += ($output + '/user-protection-entry.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/stack-switch.obj'),
    (Join-Path $project 'arch/x86/kernel/stack/assembly/switch/main.asm'))
$objects += ($output + '/stack-switch.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/apic-capture.obj'),
    (Join-Path $project 'arch/x86/kernel/interrupts/apic/assembly/profile/capture.asm'))
$objects += ($output + '/apic-capture.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$output+'/uart-wait.obj'),
    (Join-Path $project 'arch/x86/kernel/interrupts/registry/assembly/wait/bounded.asm'))
$objects += ($output+'/uart-wait.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$output+'/pm-read.obj'),
    (Join-Path $project 'arch/x86/kernel/timer/lapic/assembly/reference/port.asm'))
$objects += ($output+'/pm-read.obj')
foreach ($entry in @('entry/return','verification/registers')) {
    $object = $output + '/irq-' + ($entry.Replace('/','-')) + '.obj'
    $irqDefines = if ($TestSchedulerPeriodic -and $entry -eq 'entry/return') { @('/DKERNEL_TEST_SCHEDULER_PERIODIC=1') } else { @() }
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') (@('/nologo','/W3','/WX','/c',('/Fo'+$object)) + $irqDefines +
        @((Join-Path $project ('arch/x86/kernel/interrupts/return/assembly/'+$entry+'.asm'))))
    $objects += $object
}
if ($TestCpuFault -ne 'none') {
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/cpu-fault.obj'),
        (Join-Path $project 'test/kernel/exceptions/assembly/fault/probe.asm'))
    $objects += ($output + '/cpu-fault.obj')
}
if ($TestWindowFault -ne 'none') {
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/fault.obj'),
        (Join-Path $project 'test/kernel/mm/window/assembly/fault/probe.asm'))
    $objects += ($output + '/fault.obj')
}
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/c',('/Fo' + (Join-Path $output 'retirement.obj')),
    (Join-Path $project 'arch/x86/mm/paging/retirement/assembly/retire/state.asm'))
$objects += Join-Path $output 'retirement.obj'
$schedulerAssembly = @('assembly/irq/compute.asm','assembly/switch/context.asm','assembly/profile/capture.asm','assembly/profile/permissions.asm')
if ($TestSchedulerPreservation) { $schedulerAssembly += '../../../../test/arch/x86/kernel/scheduler/assembly/preservation/probe.asm' }
foreach ($source in $schedulerAssembly) {
    $object = $output + '/scheduler-' + $source.Replace('/','-').Replace('.asm','.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$object),
        (Join-Path $project ('arch/x86/kernel/scheduler/'+$source)))
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
if ($TestFramebufferMapping -or $TestConsoleDraw -or $ConsoleShell -or $TestApService -like 'ap-console*') {
    $consoleSources = @('kernel/console/video/console.c','kernel/console/video/render.c',
        'kernel/console/video/service/console.c')
    if ($TestFramebufferMapping) { $consoleSources += @('test/kernel/console/video/kernel/probe.c','test/kernel/console/video/kernel/render_probe.c','test/kernel/console/video/kernel/service_probe.c') }
    if ($TestConsoleDraw) { $consoleSources += @('kernel/console/video/measurement/budget.c','kernel/console/video/batch/render.c','test/kernel/console/video/active/worker.c','test/kernel/console/video/active/snapshot.c','test/kernel/console/video/profiling/profile.c') }
    if ($ConsoleShell) { $consoleSources += @('kernel/console/video/measurement/budget.c',
        'kernel/console/video/batch/render.c','arch/x86/kernel/drivers/video/framebuffer/live/group.c',
        'arch/x86/kernel/drivers/video/framebuffer/console/runtime.c',
        'arch/x86/kernel/drivers/video/framebuffer/console/requests.c',
        'kernel/console/video/requests/queue.c','kernel/console/video/requests/consumer.c',
        'kernel/console/video/pointer/state.c') }
    if ($ProfileVgpuFlush) { $consoleSources += 'arch/x86/kernel/drivers/video/framebuffer/console/profile.c'; if (!$VgpuIommu) { $consoleSources += 'kernel/diagnostics/latency.c' } }
    foreach ($source in $consoleSources) {
        $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
        $objects += $object
    }
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj',
        '--target','x86_64-pc-windows-msvc','-C','panic=abort','-C','opt-level=2',
        '-C','overflow-checks=yes','-D','warnings',
        (Join-Path $project 'kernel/console/video/rust/cursor/plan.rs'),'-o',($output + '/console-grid.obj'))
    $objects += ($output + '/console-grid.obj')
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj',
        '--target','x86_64-pc-windows-msvc','-C','panic=abort','-C','opt-level=2',
        '-C','overflow-checks=yes','-D','warnings',
        (Join-Path $project 'kernel/console/video/rust/text/utf8.rs'),'-o',($output + '/console-text.obj'))
    $objects += ($output + '/console-text.obj')
    if ($TestConsoleDraw -or $ConsoleShell) {
        Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj',
            '--target','x86_64-pc-windows-msvc','-C','panic=abort','-C','opt-level=2',
            '-C','overflow-checks=yes','-D','warnings',
            (Join-Path $project 'kernel/console/video/rust/ffi/timing.rs'),'-o',($output + '/console-timing.obj'))
        $objects += ($output + '/console-timing.obj')
    }
}
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/drivers/video/framebuffer/rust/ffi/mode.rs'),
    '-o',($output + '/framebuffer-fields.obj'))
$objects += ($output + '/framebuffer-fields.obj')
. (Join-Path $project 'kernel/console/video/scripts/build/pointer.ps1')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=staticlib','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-C','lto=fat','-C','codegen-units=1','-C','overflow-checks=yes','-D','warnings','--cfg','scheduler_core_lock',
    (Join-Path $project 'kernel/scheduler/rust/ffi/gateway.rs'),'-o',($output+'/scheduler.lib'))
$objects += ($output+'/scheduler.lib')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/devices/rust/encoding/validation.rs'),
    '-o',($output + '/lapic-encoding.obj'))
$objects += ($output + '/lapic-encoding.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'lib/firmware/acpi/rust/rsdp/validation.rs'),
    '-o',($output + '/acpi-rsdp-fields.obj'))
$objects += ($output + '/acpi-rsdp-fields.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/firmware/acpi/rust/root/layout.rs'),
    '-o',($output + '/acpi-root-layout.obj'))
$objects += ($output + '/acpi-root-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/firmware/acpi/madt/rust/records/validation.rs'),
    '-o',($output + '/madt-records.obj'))
$objects += ($output + '/madt-records.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/profile/validation.rs'),
    '-o',($output + '/apic-profile.obj'))
$objects += ($output + '/apic-profile.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/registers/validation.rs'),
    '-o',($output + '/apic-registers.obj'))
$objects += ($output + '/apic-registers.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/return/rust/frame/validation.rs'),
    '-o',($output + '/irq-frame.obj'))
$objects += ($output + '/irq-frame.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/registry/rust/subscription/validation.rs'),
    '-o',($output + '/irq-subscription.obj'))
$objects += ($output + '/irq-subscription.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/timer/lapic/rust/calibration/validation.rs'),
    '-o',($output + '/timer-calibration.obj'))
$objects += ($output + '/timer-calibration.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/timer/hpet/description/rust/validation/fields.rs'),
    '-o',($output + '/hpet-description.obj'))
$objects += ($output + '/hpet-description.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/timer/hpet/watchdog/rust/deadline/policy.rs'),
    '-o',($output + '/hpet-watchdog.obj'))
$objects += ($output + '/hpet-watchdog.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/timer/hpet/watchdog/rust/selection/routes.rs'),
    '-o',($output + '/hpet-watchdog-routes.obj'))
$objects += ($output + '/hpet-watchdog-routes.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/timer/hpet/watchdog/rust/recovery/policy.rs'),
    '-o',($output + '/hpet-watchdog-recovery.obj'))
$objects += ($output + '/hpet-watchdog-recovery.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/control/rust/encoding/validation.rs'),
    '-o',($output + '/irq-control.obj'))
$objects += ($output + '/irq-control.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/serial/interrupts/rust/routing/validation.rs'),
    '-o',($output + '/uart-routing.obj'))
$objects += ($output + '/uart-routing.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/serial/transmit/rust/bounds/validation.rs'),
    '-o',($output + '/uart-tx-bounds.obj'))
$objects += ($output + '/uart-tx-bounds.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/serial/queue/rust/state/validation.rs'),
    '-o',($output + '/uart-queue-state.obj'))
$objects += ($output + '/uart-queue-state.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/paging/rust/validation/entries.rs'),
    '-o',($output + '/paging-entries.obj'))
$objects += ($output + '/paging-entries.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/drivers/video/framebuffer/rust/tables/contents.rs'),
    '-o',($output + '/framebuffer-tables.obj'))
$objects += ($output + '/framebuffer-tables.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/discovery/rust/ffi/gateway.rs",'-o',"$output/vgpu-fields.obj")
$objects += "$output/vgpu-fields.obj"
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/mapping/rust/ffi/pages.rs",'-o',"$output/vgpu-pages.obj")
$objects += "$output/vgpu-pages.obj"
if ($TestVgpuQueues) { . (Join-Path $PSScriptRoot 'build/vgpu-queues.ps1') }
if ($TestVgpuResources) { . (Join-Path $PSScriptRoot 'build/vgpu-resources.ps1') }
if ($TestVgpuIommu -or $VgpuIommu) { . (Join-Path $PSScriptRoot 'build/vgpu-iommu.ps1') }
if ($TestIommuDiscovery -or $TestPciEcam) { . (Join-Path $PSScriptRoot 'build/iommu-discovery.ps1') }
if ($TestIommuRegisters) { . (Join-Path $PSScriptRoot 'build/iommu-registers.ps1') }
if ($TestVgpuInitialization) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/initialization/rust/ffi/policy.rs",'-o',"$output/vgpu-policy.obj")
    $objects += "$output/vgpu-policy.obj"
}
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',
    (Join-Path $project 'arch/x86/kernel/drivers/video/framebuffer/rust/drawing/validation.rs'),
    '-o',($output + '/framebuffer-drawing.obj'))
$objects += ($output + '/framebuffer-drawing.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/regions/rust/layout/validation.rs'),
    '-o',($output + '/region-layout.obj'))
$objects += ($output + '/region-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/physical/rust/alignment/placement.rs'),
    '-o',($output + '/physical-alignment.obj'))
$objects += ($output + '/physical-alignment.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/descriptors/rust/layout/validation.rs'),
    '-o',($output + '/ist-layout.obj'))
$objects += ($output + '/ist-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/descriptors/rust/protection/policy.rs'),
    '-o',($output + '/user-protection-policy.obj'))
$objects += ($output + '/user-protection-policy.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/exceptions/rust/frame/validation.rs'),
    '-o',($output + '/exception-frame.obj'))
$objects += ($output + '/exception-frame.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/rust/regions/validation.rs'),
    '-o',($output + '/validation.obj'))
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/window/rust/layout/validation.rs'),
    '-o',($output + '/window-layout.obj'))
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/objects/rust/validation/bounds.rs'),
    '-o',($output + '/object-bounds.obj'))
. (Join-Path $project 'kernel/drivers/input/ps2/scripts/build/controller.ps1')
if ($TestPs2Decoder) {
    $object = Join-Path $output 'ps2-decoder-abi.obj'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'test/kernel/drivers/input/ps2/decoder/abi_test.c'),('/Fo' + $object)))
    $objects += $object
}
if ($TestPs2Decoder -or $Ps2Input) {
    $object = Join-Path $output 'ps2-decoder.obj'
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
        '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/input/integration/rust/ffi/ps2.rs'),'-o',$object)
    $objects += $object
}
. (Join-Path $project 'kernel/sync/scripts/build.ps1')
if ($ApRuntime -or $TestApTransition -in @('image','image-rollback','joint','joint-fail-before','joint-fail-after','joint-panic')) {
    . (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/installation/scripts/build.ps1')
}
$userPrograms = Join-Path $project 'userspace/init'
if (($UserProcess -or $TestK6Process) -and !$PlanOnly) {
    $userTarget = Join-Path (& $rustc --print sysroot) 'lib/rustlib/x86_64-unknown-none'
    if (!(Test-Path -LiteralPath $userTarget)) { throw 'Target Rust x86_64-unknown-none assente: rustup target add x86_64-unknown-none' }
}
if ($UserProcess) {
    Invoke-Compiler $rustc @('--edition=2024','-D','warnings','--crate-type=bin','--crate-name','user_init','--target','x86_64-unknown-none',
        '-C','panic=abort','-C','opt-level=s','-C','relocation-model=static',"-C","link-arg=-T$userPrograms/link.ld",
        '-C','link-arg=-zmax-page-size=4096','-C','link-arg=--build-id=none','-C','link-arg=--no-gc-sections',"$userPrograms/init.rs",'-o',"$output/user-init.elf")
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror',"-I$output/",'-o',"$output/user-init-image.obj","$project/kernel/process/programs/image.asm")
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'kernel/process/boot.c'),("/Fo$output/k6-boot.obj")))
    $objects += @("$output/user-init-image.obj","$output/k6-boot.obj")
}
if ($UserProcess -and $Ps2Input) {
    $bridgeCfg = if ($TestShellBridgeFault) { @('--cfg','bridge_test_fault') } else { @() }
    Invoke-Compiler $rustc (@('--edition=2024','-D','warnings','--crate-type=bin','--crate-name','user_bridge','--target','x86_64-unknown-none',
        '-C','panic=abort','-C','opt-level=s','-C','relocation-model=static','-C',"link-arg=-T$userPrograms/link.ld",
        '-C','link-arg=-zmax-page-size=4096','-C','link-arg=--build-id=none','-C','link-arg=--no-gc-sections',
        "$project/bridge/service/rust/status/main.rs",'-o',"$output/user-bridge.elf") + $bridgeCfg)
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror',"-I$output/",'-o',"$output/user-bridge-image.obj","$project/kernel/process/programs/bridge.asm")
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/arch/x86/kernel/drivers/input/ps2/shell/bridge.c","/Fo$output/shell-bridge.obj"))
    $objects += @("$output/user-bridge-image.obj","$output/shell-bridge.obj")
}
if ($UserProcess -or $TestK6Process) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--crate-name','k6_process','--emit=obj','--target','x86_64-pc-windows-msvc',
        '-D','warnings','-C','panic=abort','-C','opt-level=2','-C','codegen-units=1',(Join-Path $project 'kernel/process/rust/lib.rs'),'-o',"$output/k6-policy.obj")
    foreach ($unit in @(@('runtime','k6-process'),@('image','k6-image'),@('syscall','k6-syscall'),@('irq','k6-irq'))) {
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project "arch/x86/kernel/process/$($unit[0]).c"),("/Fo$output/$($unit[1]).obj")))
    }
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'arch/x86/kernel/process/resident.c'),("/Fo$output/k6-resident.obj")))
    $k6Assembly = @('/nologo','/W3','/WX','/c')
    if ($TestK6Process) { $k6Assembly += '/DK6_DIAGNOSTICS=1' }
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') ($k6Assembly + @(("/Fo$output/k6-transition.obj"),
        (Join-Path $project 'arch/x86/kernel/process/assembly/entry/transition.asm')))
    $objects += @("$output/k6-policy.obj","$output/k6-process.obj","$output/k6-image.obj","$output/k6-syscall.obj","$output/k6-irq.obj","$output/k6-resident.obj","$output/k6-transition.obj")
}
if ($TestK6Process) {
    $programRoot = Join-Path $project 'test/kernel/process'
    foreach ($program in 'hello','null','readonly','privileged','kernel_jump','bad_pointer','bss','irq','spin','syscall_spin','flags_spin','flags_ac','write_flood','spurious','yield_a','yield_b','ipc_client','ipc_server','bridge_client','bridge_server') {
        Invoke-Compiler $rustc @('--edition=2024','-D','warnings','--crate-type=bin','--crate-name',"k6_$program",'--target','x86_64-unknown-none',
            '--cfg',"program_$program",'-C','panic=abort','-C','opt-level=s','-C','relocation-model=static',
            '-C',"link-arg=-T$userPrograms/link.ld",'-C','link-arg=-zmax-page-size=4096','-C','link-arg=--build-id=none',
            '-C','link-arg=--no-gc-sections',"$programRoot/programs/program.rs",'-o',"$output/k6-$program.elf")
    }
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror',"-I$output/",'-o',"$output/k6-programs.obj","$programRoot/integration/programs.asm")
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$programRoot/integration/probe.c",("/Fo$output/k6-probe.obj")))
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project 'arch/x86/kernel/process/hooks.c'),("/Fo$output/k6-hooks.obj")))
    $objects += @("$output/k6-programs.obj","$output/k6-probe.obj","$output/k6-hooks.obj")
    $keyboardSources = [ordered]@{
        'k6-keyboard' = 'test/kernel/process/integration/keyboard.c'
        'k6-keyboard-route' = 'arch/x86/kernel/drivers/input/ps2/runtime/route.c'
        'k6-keyboard-controller' = 'kernel/drivers/input/ps2/controller/controller.c'
        'k6-keyboard-discovery' = 'kernel/drivers/input/ps2/controller/discovery.c'
        'k6-keyboard-transport' = 'kernel/drivers/input/ps2/controller/transport.c'
    }
    foreach ($entry in $keyboardSources.GetEnumerator()) {
        $object = "$output/$($entry.Key).obj"
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $entry.Value),"/Fo$object"))
        $objects += $object
    }
}
if ($TestK5Copy) {
    $probeCopy = Join-Path $project 'test/arch/x86/kernel/interrupts/privilege'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$probeCopy/copy.c",("/Fo$output/k5-copy.obj")))
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',
        ("/Fo$output/k5-copy-entry.obj"),"$probeCopy/assembly/copy/entry.asm")
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
        '-D','warnings','-C','panic=abort','-C','opt-level=2',"$probeCopy/rust/copy/validation.rs",'-o',"$output/k5-copy-validation.obj")
    $objects += @("$output/k5-copy.obj","$output/k5-copy-entry.obj","$output/k5-copy-validation.obj")
}
if ($TestK5Syscall -or $TestK5Copy) {
    $probeSys = Join-Path $project 'test/arch/x86/kernel/interrupts/privilege'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$probeSys/syscall.c",("/Fo$output/k5-syscall.obj")))
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',
        ("/Fo$output/k5-syscall-entry.obj"),"$probeSys/assembly/syscall/entry.asm")
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
        '-D','warnings','-C','panic=abort','-C','opt-level=2',"$probeSys/rust/syscall/validation.rs",'-o',"$output/k5-syscall-validation.obj")
    $objects += @("$output/k5-syscall.obj","$output/k5-syscall-entry.obj","$output/k5-syscall-validation.obj")
}
if ($TestK5Privilege -or $TestK5Syscall -or $TestK5Copy -or $TestApService -eq 'k5-ap') {
    $probe = Join-Path $project 'test/arch/x86/kernel/interrupts/privilege'
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$probe/probe.c",("/Fo$output/k5-probe.obj")))
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',
        ("/Fo$output/k5-entry.obj"),"$probe/assembly/probe/entry.asm")
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
        '-D','warnings','-C','panic=abort','-C','opt-level=2',"$probe/rust/frame/validation.rs",'-o',"$output/k5-validation.obj")
    $objects += @("$output/k5-probe.obj","$output/k5-entry.obj","$output/k5-validation.obj")
}
# Windows limits a command line to 32767 characters: the object list goes through a response file.
$linkArguments = @('/nologo','/machine:x64','/subsystem:native',
    '/entry:kernel_entry','/nodefaultlib','/incremental:no','/dynamicbase:no','/fixed','/Brepro',('/base:0x' + $OsImageBase.ToString('X')),
    ('/out:' + $output + '/kernel.exe'),('/map:' + $output + '/kernel.map'),($output + '/io.obj'),($output + '/validation.obj'),($output + '/window-layout.obj'),
    ($output + '/object-bounds.obj')) + $objects
$linkResponse = Join-Path $output 'kernel.link.rsp'
if ($PlanOnly) {
    Write-PlanCommand 'link.exe' $linkArguments
    Write-PlanCommand 'convert-image.ps1' @('-Image', ($output + '/kernel.exe'), '-Output', ($output + '/kernel.elf'))
    return
}
Set-Content -LiteralPath $linkResponse -Encoding ascii -Value ($linkArguments | ForEach-Object { '"' + $_ + '"' })
Invoke-Compiler (Join-Path $ToolchainBin 'link.exe') @('@' + $linkResponse)
& (Join-Path $PSScriptRoot 'convert-image.ps1') -Image ($output + '/kernel.exe') -Output ($output + '/kernel.elf')
Write-BootStatus OK "Kernel ELF verificato e generato: $output/kernel.elf"
if ($Install) {
    $esp = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT'
    if (!(Test-Path -LiteralPath (Join-Path $esp 'BOOTX64.EFI'))) { throw 'Prima costruire il bootloader con -BootKernel.' }
    Copy-Item -LiteralPath (Join-Path $output 'kernel.elf') -Destination (Join-Path $esp 'kernel.elf')
    Write-BootStatus OK 'Kernel installato nella ESP generata; usare un bootloader costruito con -BootKernel.'
}
