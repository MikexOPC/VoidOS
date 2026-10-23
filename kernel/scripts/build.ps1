param([ValidateSet('none','guard-low','guard-high','nx','retired','rollback','blocked','irq-timeout','idle-timeout','idle-repeat-timeout')][string]$TestSchedulerFault = 'none',
    [string]$ToolchainBin, [switch]$TestBootstrapMemory, [switch]$TestPs2Decoder, [switch]$TestPs2Controller, [switch]$Ps2Input, [switch]$Ps2Mouse, [switch]$TestPanic, [string]$OutputDirectory, [switch]$Install, [switch]$EventLoop,
    [switch]$TestWatchdogLostLapic, [switch]$TestFramebufferMapping, [switch]$TestConsoleActive, [switch]$TestConsoleDraw,
    [switch]$TestWatchdogAlternate, [switch]$TestConsoleBatch, [switch]$ConsoleShell, [switch]$InteractiveInput,
    [switch]$TestWatchdogCleanupRecovery, [switch]$TestSchedulerPreservation, [switch]$TestIommuActivation, [switch]$TestIommuDma, [switch]$TestVgpuMapping, [switch]$TestVgpuInitialization, [switch]$TestVgpuQueues, [switch]$TestVgpuResources, [switch]$TestVgpuResourceReject, [switch]$TestVgpuScanout, [switch]$TestVgpuIommu, [switch]$TestVgpuIommuFault, [switch]$TestIommuDiscovery, [switch]$TestIommuRegisters, [switch]$TestIommuDomain, [switch]$TestIommuQi, [switch]$TestPciEcam, [switch]$TestPciEcamAbsent, [switch]$TestPciTopology, [switch]$TestIommuRouting,
    [ValidateSet('none','execute','unmapped','guard-low','guard-high')][string]$TestWindowFault = 'none',
    [ValidateSet('none','opcode','selector','divide','stack','nested','double')][string]$TestCpuFault = 'none',
    [ValidateSet('none','retired','guard-high','execute','return')][string]$TestMainFault = 'none',
    [ValidateSet('none','retired','guard-low','guard-high','execute')][string]$TestRegionFault = 'none')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build/profiles.ps1')
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
if (!$ToolchainBin) {
    $version = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin/Hostx64/x64'
}
$output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $project 'kernel/build' }
New-Item -ItemType Directory -Force -Path $output | Out-Null

# ====== ogni compilazione deve terminare senza errori ===== #

function Invoke-Compiler([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita: $program" }
}
Write-BootSection 'kernel autonomo C e Rust'
$sdk = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Header UCRT del Windows SDK assenti.' }
$flags = @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/O2','/c',('/I' + $project),
    ('/I' + (Join-Path $ToolchainBin '../../../include')),('/I' + (Join-Path $sdk.FullName 'ucrt')), '/DKERNEL_SCHEDULER_IRQ=1')
if ($TestPs2Decoder) { $flags += '/DKERNEL_TEST_PS2_DECODER=1' }
if ($TestFramebufferMapping) { $flags += '/DKERNEL_TEST_FRAMEBUFFER_MAPPING=1' }
if ($TestVgpuMapping) { $flags += '/DKERNEL_TEST_VGPU_MAPPING=1' }
if ($TestVgpuInitialization) { $flags += '/DKERNEL_TEST_VGPU_INITIALIZATION=1' }
if ($TestVgpuQueues) { $flags += '/DKERNEL_TEST_VGPU_QUEUES=1' }
if ($TestVgpuResources) { $flags += '/DKERNEL_TEST_VGPU_RESOURCES=1' }
if ($TestVgpuResourceReject) { $flags += '/DKERNEL_TEST_VGPU_RESOURCE_REJECT=1' }
if ($TestVgpuScanout) { $flags += '/DKERNEL_TEST_VGPU_SCANOUT=1' }
if ($TestVgpuIommu) { $flags += '/DKERNEL_TEST_VGPU_IOMMU=1' }
if ($TestVgpuIommuFault) { $flags += '/DKERNEL_TEST_VGPU_IOMMU_FAULT=1' }
if ($TestIommuDiscovery) { $flags += '/DKERNEL_TEST_IOMMU_DISCOVERY=1' }
if ($TestIommuRegisters) { $flags += '/DKERNEL_TEST_IOMMU_REGISTERS=1' }
if ($TestIommuDomain) { $flags += '/DKERNEL_TEST_IOMMU_DOMAIN=1' }
if ($TestIommuQi) { $flags += '/DKERNEL_TEST_IOMMU_QI=1' }
if ($TestPciEcam) { $flags += '/DKERNEL_TEST_PCI_ECAM=1'; if ($TestPciEcamAbsent) { $flags += '/DKERNEL_TEST_PCI_ECAM_ABSENT=1' }; if ($TestPciTopology) { $flags += '/DKERNEL_TEST_PCI_TOPOLOGY=1' }; if ($TestIommuRouting) { $flags += '/DKERNEL_TEST_IOMMU_ROUTING=1' }; if ($TestIommuActivation) { $flags += '/DKERNEL_TEST_IOMMU_ACTIVATION=1' }; if ($TestIommuDma) { $flags += '/DKERNEL_TEST_IOMMU_DMA=1' } }
if ($TestConsoleActive) { $flags += '/DKERNEL_TEST_CONSOLE_ACTIVE=1' }
if ($TestConsoleDraw) { $flags += '/DKERNEL_TEST_CONSOLE_DRAW=1' }
if ($TestConsoleBatch) { $flags += '/DKERNEL_TEST_CONSOLE_BATCH=1' }
if ($ConsoleShell) { $flags += '/DKERNEL_CONSOLE_SHELL=1' }
if ($InteractiveInput) { $flags += '/DKERNEL_INPUT_INTERACTIVE=1' }
if ($TestPs2Controller) { $flags += '/DKERNEL_TEST_PS2_CONTROLLER=1' }
if ($Ps2Input) { $flags += '/DKERNEL_PS2_INPUT=1' }
if ($Ps2Mouse) { $flags += '/DKERNEL_PS2_MOUSE=1' }
if ($bootstrapDiagnostics) { $flags += '/DKERNEL_TEST_BOOTSTRAP_MEMORY=1' }
if ($TestSchedulerFault -ne 'none') {
    $mode = switch ($TestSchedulerFault) { 'guard-low' {1} 'guard-high' {2} 'nx' {3} 'retired' {4} 'rollback' {5} 'blocked' {6} 'irq-timeout' {7} 'idle-timeout' {8} 'idle-repeat-timeout' {9} }
    $flags += ('/DKERNEL_TEST_SCHEDULER_FAULT=' + $mode)
}
if ($TestSchedulerPreservation) { $flags += '/DKERNEL_TEST_SCHEDULER_PRESERVATION=1' }
if ($TestPanic) { $flags += '/DKERNEL_TEST_PANIC=1' }
if ($EventLoop) { $flags += '/DKERNEL_EVENT_LOOP=1' }
if ($TestWatchdogLostLapic) { $flags += '/DKERNEL_TEST_WATCHDOG_LOST_LAPIC=1' }
if ($TestWatchdogAlternate) { $flags += '/DKERNEL_TEST_WATCHDOG_ALTERNATE=1' }
if ($TestWatchdogCleanupRecovery) { $flags += '/DKERNEL_TEST_WATCHDOG_CLEANUP_RECOVERY=1' }
if ($Install -and $TestWatchdogLostLapic) { throw 'Non installare la fault injection watchdog.' }
if ($TestRegionFault -ne 'none') {
    $mode = switch ($TestRegionFault) { 'retired' {1} 'guard-low' {2} 'guard-high' {3} 'execute' {4} }
    $flags += ('/DKERNEL_TEST_REGION_FAULT=' + $mode)
}
if ($TestMainFault -ne 'none') {
    $mode = switch ($TestMainFault) { 'retired' {1} 'guard-high' {2} 'execute' {3} 'return' {4} }
    $flags += ('/DKERNEL_TEST_MAIN_FAULT=' + $mode)
}
if ($TestWindowFault -ne 'none') {
    $mode = switch ($TestWindowFault) { 'execute' {1} 'unmapped' {2} 'guard-low' {3} 'guard-high' {4} }
    $flags += ('/DKERNEL_TEST_WINDOW_FAULT=' + $mode)
}
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
    'arch/x86/kernel/scheduler/integration/kernel.c','arch/x86/kernel/scheduler/integration/workload.c',
    'arch/x86/kernel/scheduler/integration/report.c','arch/x86/kernel/scheduler/integration/validation.c','arch/x86/kernel/scheduler/idle/idle.c',
    'arch/x86/kernel/scheduler/persistent/workload.c',
    'arch/x86/kernel/scheduler/dispatcher/engine.c','arch/x86/kernel/scheduler/diagnostic/profiles.c',
    'arch/x86/kernel/scheduler/irq/bridge.c','arch/x86/kernel/scheduler/runtime/memory.c','arch/x86/kernel/scheduler/runtime/payload.c','arch/x86/kernel/entry.c','arch/x86/kernel/serial.c','arch/x86/mm/window/page.c','arch/x86/mm/window/access.c','kernel/main.c','kernel/startup/early.c','kernel/startup/platform.c','kernel/startup/devices.c','kernel/startup/profile.c',
    'kernel/bootstrap/memory.c','kernel/boot/contract.c','kernel/drivers/video/framebuffer/handoff.c','kernel/drivers/video/framebuffer/mapping.c',
    'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
    'kernel/drivers/video/virtio/mapping/plan.c','arch/x86/mm/paging/iommu/iommu_registry.c','arch/x86/mm/paging/surface/registry.c','arch/x86/mm/paging/gpu/authority.c','arch/x86/mm/paging/gpu/validation.c',
    'kernel/drivers/video/virtio/discovery/configuration/read.c',
    'arch/x86/kernel/drivers/video/virtio/preflight.c','arch/x86/kernel/drivers/video/virtio/construction.c',
    'arch/x86/kernel/drivers/video/virtio/lifecycle.c','arch/x86/kernel/drivers/video/virtio/pci/access.c',
    'arch/x86/kernel/drivers/video/virtio/integration/kernel.c',
    'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/objects/pool.c','kernel/diagnostics/log.c',
    'arch/x86/mm/regions/mapping.c','arch/x86/mm/regions/access.c',
    'arch/x86/mm/arena/objects.c',
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
    'arch/x86/kernel/interrupts/apic/probe.c','arch/x86/kernel/interrupts/apic/access.c',
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
    'arch/x86/kernel/exceptions/terminal.c','arch/x86/kernel/descriptors/stacks.c','arch/x86/kernel/descriptors/segments.c',
    'arch/x86/kernel/descriptors/stack_access.c','arch/x86/kernel/stack/main.c','arch/x86/lib/descriptors/gdt.c','arch/x86/lib/descriptors/tss.c','lib/memory/address_ranges.c') + $diagnosticSources)) {
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
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$object),
        (Join-Path $project ('arch/x86/kernel/interrupts/return/assembly/'+$entry+'.asm')))
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
$schedulerAssembly = @('assembly/irq/compute.asm','assembly/switch/context.asm','assembly/profile/capture.asm','assembly/profile/permissions.asm')
if ($TestSchedulerPreservation) { $schedulerAssembly += '../../../../test/arch/x86/kernel/scheduler/assembly/preservation/probe.asm' }
foreach ($source in $schedulerAssembly) {
    $object = $output + '/scheduler-' + $source.Replace('/','-').Replace('.asm','.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$object),
        (Join-Path $project ('arch/x86/kernel/scheduler/'+$source)))
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
if ($TestFramebufferMapping -or $TestConsoleDraw -or $ConsoleShell) {
    $consoleSources = @('kernel/console/video/console.c','kernel/console/video/render.c',
        'kernel/console/video/service/console.c')
    if ($TestFramebufferMapping) { $consoleSources += @('test/kernel/console/video/kernel/probe.c','test/kernel/console/video/kernel/render_probe.c','test/kernel/console/video/kernel/service_probe.c') }
    if ($TestConsoleDraw) { $consoleSources += @('kernel/console/video/measurement/budget.c','kernel/console/video/batch/render.c','test/kernel/console/video/active/worker.c','test/kernel/console/video/active/snapshot.c','test/kernel/console/video/profiling/profile.c') }
    if ($ConsoleShell) { $consoleSources += @('kernel/console/video/measurement/budget.c',
        'kernel/console/video/batch/render.c','arch/x86/kernel/drivers/video/framebuffer/live/group.c',
        'arch/x86/kernel/drivers/video/framebuffer/console/runtime.c','kernel/console/video/pointer/state.c') }
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
    '-C','panic=abort','-C','opt-level=2','-C','lto=fat','-C','codegen-units=1','-C','overflow-checks=yes','-D','warnings',
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
if ($TestVgpuIommu) { . (Join-Path $PSScriptRoot 'build/vgpu-iommu.ps1') }
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
Invoke-Compiler (Join-Path $ToolchainBin 'link.exe') (@('/nologo','/machine:x64','/subsystem:native',
    '/entry:kernel_entry','/nodefaultlib','/incremental:no','/dynamicbase:no','/fixed','/base:0x100000000',
    ('/out:' + $output + '/kernel.exe'),('/map:' + $output + '/kernel.map'),($output + '/io.obj'),($output + '/validation.obj'),($output + '/window-layout.obj'),
    ($output + '/object-bounds.obj')) + $objects)
& (Join-Path $PSScriptRoot 'convert-image.ps1') -Image ($output + '/kernel.exe') -Output ($output + '/kernel.elf')
Write-BootStatus OK "Kernel ELF verificato e generato: $output/kernel.elf"
if ($Install) {
    $esp = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT'
    if (!(Test-Path -LiteralPath (Join-Path $esp 'BOOTX64.EFI'))) { throw 'Prima costruire il bootloader con -BootKernel.' }
    Copy-Item -LiteralPath (Join-Path $output 'kernel.elf') -Destination (Join-Path $esp 'kernel.elf')
    Write-BootStatus OK 'Kernel installato nella ESP generata; usare un bootloader costruito con -BootKernel.'
}
