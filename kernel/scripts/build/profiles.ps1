# ============ isolated synchronization profile ============

if ($TestApStress -ne 'none') {
    foreach ($key in $PSBoundParameters.Keys) {
        if ($key -notin @('TestApStress','OutputDirectory','ToolchainBin')) { throw 'AP stress requires an isolated ordinary-startup profile.' }
    }
}

if ($TestKernelSyncFault -ne 'none') { $TestKernelSync = $true }
if ($TestKernelSync) {
    foreach ($key in $PSBoundParameters.Keys) {
        if ($key -notin @('TestKernelSync','TestKernelSyncFault','TestBootstrapMemory','OutputDirectory','ToolchainBin')) {
            throw 'Synchronization probes require an isolated diagnostic image.'
        }
    }
}
# ============ isolated loader retirement fault ============

if ($TestLoaderRetired) {
    foreach ($key in $PSBoundParameters.Keys) {
        if ($key -notin @('TestLoaderRetired','TestBootstrapMemory','OutputDirectory','ToolchainBin')) {
            throw 'Loader retirement fault requires an isolated diagnostic image.'
        }
    }
}
# ============ profile validation and derived options ============

if ($TestIommuDma) { $TestIommuActivation=$true }
if ($TestIommuActivation) { $TestIommuRouting=$true }
if ($TestIommuRouting) { $TestPciTopology=$true }
if ($TestPciEcamAbsent) { $TestPciEcam=$true }
if ($TestPciTopology) { $TestPciEcam=$true; if ($TestPciEcamAbsent) {throw 'Topology requires actual MCFG, not the absence filter.'} }
if ($TestIommuQi) { $TestIommuDomain=$true }
if ($TestIommuDomain) { $TestIommuRegisters=$true }
if ($TestIommuRegisters) { $TestIommuDiscovery=$true }
if ($TestPciEcam -and $TestIommuDiscovery) {throw 'ECAM and IOMMU diagnostics use separate images.'}
if (($TestIommuDiscovery -or $TestPciEcam) -and ($Install -or $Ps2Input -or $Ps2Mouse -or $ConsoleShell -or $InteractiveInput -or $EventLoop -or
    $TestVgpuMapping -or $TestVgpuInitialization -or $TestVgpuQueues -or $TestVgpuResources -or $TestVgpuScanout -or $TestVgpuResourceReject -or
    $TestFramebufferMapping -or $TestConsoleActive -or $TestConsoleDraw -or $TestConsoleBatch -or $TestBootstrapMemory -or $TestPanic -or
    $TestPs2Decoder -or $TestPs2Controller -or $TestWatchdogLostLapic -or $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery -or
    $TestSchedulerPreservation -or $TestSchedulerFault -ne 'none' -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or
    $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'IOMMU discovery diagnostic must stay isolated.' }
if ($TestVgpuScanout -and $TestVgpuResourceReject) { throw 'Choose resource rejection or visual presentation.' }
if ($TestVgpuIommuFault) { if ($TestVgpuScanout) { throw 'The V1 revocation runs on the resource path, not the visual one.' }; $TestVgpuIommu=$true }
if ($VgpuIommu) { if (!$ConsoleShell) { throw 'Persistent GPU translation (V2b) belongs to the console session.' }; if ($TestVgpuIommu -or $TestVgpuInitialization) { throw 'Choose the V2b session or the VirtIO diagnostics.' } }
if ($TestVgpuIommu) { if ($TestVgpuResourceReject) { throw 'Choose V1 translation or resource rejection.' }; $TestVgpuResources=$true }
if ($TestVgpuResourceReject -or $TestVgpuScanout) { $TestVgpuResources=$true }
if ($TestVgpuResources) { $TestVgpuQueues=$true }
if ($TestVgpuQueues) { $TestVgpuInitialization=$true }
if ($TestVgpuInitialization -and ($Install -or $Ps2Input -or $Ps2Mouse -or $ConsoleShell -or $InteractiveInput -or
    $EventLoop -or $TestVgpuMapping -or $TestFramebufferMapping -or $TestConsoleActive -or $TestConsoleDraw -or
    $TestConsoleBatch -or $TestBootstrapMemory -or $TestPanic -or $TestPs2Decoder -or $TestPs2Controller -or
    $TestWatchdogLostLapic -or $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery -or $TestSchedulerPreservation -or
    $TestSchedulerFault -ne 'none' -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or
    $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'VirtIO initialization diagnostic must stay isolated.' }

if ($Ps2Mouse -and ($TestConsoleActive -or $TestConsoleDraw -or $TestConsoleBatch)) { throw 'Mouse runtime excludes console diagnostic profiles.' }
if ($Ps2Mouse) { $Ps2Input = $true }
if ($ConsoleShell) {
    if ($TestConsoleActive -or $TestConsoleDraw -or $TestConsoleBatch -or $TestFramebufferMapping) {
        throw 'ConsoleShell is separate from console diagnostic profiles.'
    }
    $Ps2Input = $true
}
if ($TestConsoleBatch) {
    if ($TestConsoleDraw) { throw 'Choose one draw diagnostic profile.' }
    $TestConsoleDraw = $true
}
if ($TestConsoleDraw) {
    if ($TestConsoleActive) { throw 'Choose one console diagnostic profile.' }
    $TestConsoleActive = $true
}
if ($TestConsoleActive) {
    if ($Install -or $Ps2Input -or $EventLoop -or $TestFramebufferMapping -or $TestBootstrapMemory -or
        $TestPanic -or $TestPs2Decoder -or $TestPs2Controller -or $TestWatchdogLostLapic -or
        $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery -or $TestSchedulerFault -ne 'none' -or
        $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or
        $TestRegionFault -ne 'none') { throw 'Active console resource diagnostic must stay isolated.' }
    $Ps2Input = $true
}
if ($TestFramebufferMapping -and ($Install -or $Ps2Input -or $EventLoop -or $TestBootstrapMemory -or
    $TestPanic -or $TestPs2Decoder -or $TestPs2Controller -or $TestWatchdogLostLapic -or
    $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery -or $TestSchedulerFault -ne 'none' -or
    $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or
    $TestRegionFault -ne 'none')) { throw 'Framebuffer mapping diagnostic must stay isolated.' }
if ($Ps2Input -and ($Install -or $EventLoop -or $TestPs2Decoder -or $TestPs2Controller -or $TestPanic -or $TestBootstrapMemory -or
    $TestSchedulerFault -ne 'none' -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or
    $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none' -or $TestWatchdogLostLapic -or
    $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery)) { throw 'PS/2 input profile must stay isolated.' }
if ($TestPs2Controller) { $TestPs2Decoder = $true }
if ($TestPs2Decoder -and ($Install -or $EventLoop -or $TestPanic -or $TestBootstrapMemory -or
    $TestSchedulerFault -ne 'none' -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or
    $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none' -or $TestWatchdogLostLapic -or
    $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery)) { throw 'PS/2 decoder diagnostic image must stay isolated.' }
if ($TestSchedulerFault -ne 'none' -and ($Install -or $EventLoop -or $TestPanic -or $TestWatchdogLostLapic -or $TestWatchdogAlternate -or $TestWatchdogCleanupRecovery -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Scheduler fault image must stay isolated; choose one fault.' }
if ($TestWatchdogCleanupRecovery -and ($EventLoop -or $TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Profilo cleanup recovery solo finito, incompatibile con altri fault.' }
if ($Install -and $TestWatchdogCleanupRecovery) { throw 'Non installare la fault injection cleanup recovery.' }
if ($TestWatchdogAlternate -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Profilo watchdog alternativo incompatibile con altri fault.' }
if ($Install -and $TestWatchdogAlternate) { throw 'Non installare immagini con route watchdog riservate per test.' }
if ($TestWatchdogLostLapic -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($Install -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Non installare immagini con fault di test nella ESP ordinaria.' }
if ($TestRegionFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($TestMainFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($TestCpuFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($Install -and $TestBootstrapMemory) { throw 'Bootstrap diagnostic image must stay isolated.' }
$bootstrapDiagnostics = $TestBootstrapMemory -or $TestWindowFault -ne 'none' -or $TestRegionFault -ne 'none'
if ($InteractiveInput -and (!$ConsoleShell -or !$Ps2Mouse)) { throw 'InteractiveInput requires ConsoleShell and Ps2Mouse' }
