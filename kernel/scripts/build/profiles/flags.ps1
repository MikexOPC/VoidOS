# ============ compiler flags by profile ============

$flags = @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/O2','/c',('/I' + $project),
    ('/I' + (Join-Path $ToolchainBin '../../../include')),('/I' + (Join-Path $sdk.FullName 'ucrt')), '/DKERNEL_SCHEDULER_IRQ=1')
if ($TestApNoNmi -and !$TestApLaunch) { throw 'NoNmi requires the AP launch diagnostic.' }
if ($TestApGroupPanic -ne 'none') {
    if (!$TestApSequence -or $TestApSequenceFault) { throw 'Group panic requires a healthy sequential diagnostic.' }
    $flags += $(if ($TestApGroupPanic -eq 'bsp') {'/DKERNEL_TEST_AP_GROUP_PANIC=0'} else {'/DKERNEL_TEST_AP_GROUP_PANIC=3'})
}
if ($TestApSequenceFault) {
    if (!$TestApSequence) { throw 'Sequence fault requires the sequence diagnostic.' }
    $flags += '/DKERNEL_TEST_AP_SEQUENCE_FAULT=1'
}
if ($TestApSequence) {
    if (!$TestApLaunch -or $TestApNoNmi -or $TestApRemotePanic) { throw 'Sequence requires a positive launch without panic.' }
    $flags += '/DKERNEL_TEST_AP_SEQUENCE=1'
}
if ($TestApRemotePanic) {
    if (!$TestApLaunch -or $TestApNoNmi) { throw 'Remote panic requires the positive AP launch diagnostic.' }
    $flags += '/DKERNEL_TEST_AP_REMOTE_PANIC=1'
}
if ($TestApLaunch) {
    if ($TestApTransition -ne 'joint-panic') { throw 'AP launch requires -TestApTransition joint-panic.' }
    $flags += $(if ($TestApNoNmi) {'/DKERNEL_TEST_AP_LAUNCH=2'} else {'/DKERNEL_TEST_AP_LAUNCH=1'})
}
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
if ($VgpuIommu) { $flags += '/DKERNEL_VGPU_IOMMU=1' }
# Flush profiling (P1-A): timers per phase, profile-only images; NoPresent keeps the console on GOP.
if ($ProfileVgpuFlush) { if (!$ConsoleShell) { throw 'Flush profiling needs the console session.' }; $flags += '/DKERNEL_PROFILE_VGPU_FLUSH=1' }
if ($ProfileVgpuNoPresent) { if (!$VgpuIommu -or !$ProfileVgpuFlush) { throw 'NoPresent needs -VgpuIommu and -ProfileVgpuFlush.' }; $flags += '/DKERNEL_PROFILE_VGPU_NO_PRESENT=1' }
if ($TestVgpuPresentFault) { if (!$VgpuIommu) { throw 'Present fault injection needs -VgpuIommu.' }; $flags += '/DKERNEL_TEST_VGPU_PRESENT_FAULT=700' }
if ($TestVgpuPresentStall) { if (!$VgpuIommu) { throw 'Present stall injection needs -VgpuIommu.' }; $flags += '/DKERNEL_TEST_VGPU_PRESENT_STALL=300' }
if ($TestKernelHeap) { $flags += '/DKERNEL_TEST_KERNEL_HEAP=1' }
if ($TestPagingTamper) { if (!$ConsoleShell) { throw 'Paging tamper test needs -ConsoleShell.' }; $flags += '/DKERNEL_TEST_PAGING_TAMPER=1' }
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
if ($TestSchedulerPreemption -or $TestSchedulerPeriodic) { $flags += '/DKERNEL_TEST_SCHEDULER_PREEMPTION=1' }
if ($TestSchedulerPeriodic) { $flags += '/DKERNEL_TEST_SCHEDULER_PERIODIC=1' }
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
if ($TestLoaderRetired) { $flags += '/DKERNEL_TEST_LOADER_RETIRED=1' }
if ($TestKernelSync) { $flags += '/DKERNEL_TEST_SYNC=1' }
if ($TestKernelSyncFault -eq 'wait') { $flags += '/DKERNEL_TEST_SYNC_FAULT=1' }
if ($TestKernelSyncFault -eq 'irq-enabled') { $flags += '/DKERNEL_TEST_SYNC_FAULT=2' }

if ($TestApResourceFault -ne 'none') {
    $mode = if ($TestApResourceFault -eq 'guard') { 1 } else { 2 }
    $flags += "/DKERNEL_TEST_AP_RESOURCE_FAULT=$mode"
}

# ============ parked APs in ordinary profiles ============

$ApRuntime = $TestApTransition -eq 'none' -and $TestSmpPanic -eq 'none' -and !$TestApCommit -and !$TestBootstrapMemory
if ($ApRuntime) { $flags += '/DKERNEL_AP_RUNTIME=1' }
if ($TestApService -ne 'none') {
    if (!$ApRuntime) { throw 'AP service requires ordinary AP admission.' }
    $serviceMode = switch ($TestApService) { 'ping' {1} 'missing-ack' {2} 'stale-ack' {3} 'panic-idle' {4} 'panic-handler' {5} 'release-missing-ack' {6} 'ap-timer' {7} 'admission' {8} 'ap-batch' {9} 'ap-batch-rollback' {9} 'ap-preempt' {9} 'ap-concurrent' {9} 'ap-concurrent-stress' {9} 'ap-concurrent-incomplete' {9} 'ap-four' {9} 'ap-four-incomplete' {9} 'ap-console' {9} 'ap-console-incomplete' {9} 'ap-console-ipi' {9} 'ap-console-ipi-no-ack' {9} 'ap-console-ipi-inflight' {9} }
    $flags += "/DKERNEL_TEST_AP_SERVICE=$serviceMode"
    if ($TestApService -in @('ap-preempt','ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete')) { $flags += '/DKERNEL_TEST_AP_PREEMPTION=1' }
    if ($TestApService -in @('ap-concurrent','ap-concurrent-stress','ap-concurrent-incomplete','ap-four','ap-four-incomplete')) { $flags += '/DKERNEL_TEST_AP_CONCURRENT=1' }
    if ($TestApService -in @('ap-concurrent-stress','ap-four','ap-four-incomplete')) { $flags += '/DKERNEL_TEST_AP_BATCH_STRESS=1' }
    if ($TestApService -in @('ap-concurrent-incomplete','ap-four-incomplete')) { $flags += '/DKERNEL_TEST_AP_NO_QUIESCE=1' }
    if ($TestApService -in @('ap-four','ap-four-incomplete')) { $flags += '/DKERNEL_TEST_AP_FOUR=1' }
    if ($TestApService -eq 'ap-batch-rollback') { $flags += '/DKERNEL_TEST_AP_BATCH_ROLLBACK=1' }
}
if ($TestApStress -ne 'none') {
    if (!$ApRuntime -or $TestPanic) { throw 'AP stress requires ordinary AP startup and its own panic trigger.' }
    $flags += $(switch ($TestApStress) {
        'withhold-ack' { '/DKERNEL_TEST_AP_STRESS_WITHHOLD_ACK=1' }
        'lock-held' { '/DKERNEL_TEST_AP_STRESS_LOCK=1' }
        'contended' { '/DKERNEL_TEST_AP_STRESS_BARRIER=1' }
    })
}

if ($TestApService -like 'ap-console*') { $flags += '/DKERNEL_TEST_AP_CONSOLE=1' }
if ($TestApService -eq 'ap-console-incomplete') { $flags += '/DKERNEL_TEST_AP_CONSOLE_INCOMPLETE=1' }

if ($TestApService -like 'ap-console-ipi*') { $flags += '/DKERNEL_TEST_AP_NOTIFY=1' }
if ($TestApService -eq 'ap-console-ipi-no-ack') { $flags += '/DKERNEL_TEST_AP_NOTIFY_NO_ACK=1' }
if ($TestApService -eq 'ap-console-ipi-inflight') { $flags += '/DKERNEL_TEST_AP_NOTIFY_INFLIGHT=1' }
