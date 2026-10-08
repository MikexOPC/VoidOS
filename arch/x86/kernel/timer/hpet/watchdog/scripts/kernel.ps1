param([switch]$UseBuilt,[string]$QemuDirectory='C:/Program Files/qemu')
$ErrorActionPreference='Stop'
$module=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project=(Resolve-Path (Join-Path $module '../../../../../..')).Path
$out=Join-Path $project ('kernel/build/watchdog-matrix-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$sources=@(Get-ChildItem $module -Recurse -File | Where-Object Extension -In '.c','.h','.rs','.ps1') +
    @(Get-ChildItem (Join-Path $module '../description'),
        (Join-Path $module '../../lapic'),(Join-Path $project 'kernel/test/timer/lapic') -Recurse -File |
        Where-Object Extension -In '.c','.h','.rs','.ps1') +
    @(Get-Item (Join-Path $project 'kernel/main.c'),(Join-Path $project 'kernel/scripts/build.ps1'))
$manifest=$sources | ForEach-Object {Get-FileHash $_.FullName}
$manifest | ConvertTo-Json | Out-File "$out/sources.json" -Encoding utf8
$normal=Join-Path $project 'kernel/build/watchdog-final'
$lost=Join-Path $project 'kernel/build/watchdog-lost-final'
$loop=Join-Path $project 'kernel/build/event-loop'
$loopLost=Join-Path $project 'kernel/build/watchdog-loop-lost-final'
$recovery=Join-Path $project 'kernel/build/watchdog-recovery-final'
$recoveryLost=Join-Path $project 'kernel/build/watchdog-recovery-lost-final'

# ============ supported build profiles with isolated fault images ============

if (!$UseBuilt) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -OutputDirectory $normal | Out-File "$out/build-normal.log"
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestWatchdogLostLapic -OutputDirectory $lost |
        Out-File "$out/build-lost.log"
    & (Join-Path $project 'kernel/scripts/build.ps1') -EventLoop -OutputDirectory $loop | Out-File "$out/build-loop.log"
    & (Join-Path $project 'kernel/scripts/build.ps1') -EventLoop -TestWatchdogLostLapic -OutputDirectory $loopLost |
        Out-File "$out/build-loop-lost.log"
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestWatchdogCleanupRecovery -OutputDirectory $recovery |
        Out-File "$out/build-recovery.log"
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestWatchdogCleanupRecovery -TestWatchdogLostLapic `
        -OutputDirectory $recoveryLost | Out-File "$out/build-recovery-lost.log"
}
Get-FileHash "$normal/kernel.elf","$lost/kernel.elf","$loop/kernel.elf","$loopLost/kernel.elf",`
    "$recovery/kernel.elf","$recoveryLost/kernel.elf" |
    ConvertTo-Json | Out-File "$out/images.json" -Encoding utf8

# ============ final-image boots, lost source and continuous stop ============

& (Join-Path $PSScriptRoot 'firmware.ps1') -ImageDirectory $normal -QemuDirectory $QemuDirectory |
    Tee-Object -FilePath "$out/normal.log"
& (Join-Path $PSScriptRoot 'firmware.ps1') -Mode lost -ImageDirectory $lost -QemuDirectory $QemuDirectory |
    Tee-Object -FilePath "$out/lost.log"
for ($index=1; $index -le 10; ++$index) {
    & (Join-Path $project 'kernel/test/timer/lapic/scripts/loop-firmware.ps1') -UseBuilt -ImageDirectory $loop `
        -QemuDirectory $QemuDirectory -StopAfterEvents 16 6>&1 | Tee-Object -FilePath "$out/loop-$index.log"
    Write-Output "Continuous watchdog boot $index/10 verified"
}
& (Join-Path $PSScriptRoot 'firmware.ps1') -Mode lost -ImageDirectory $loopLost -QemuDirectory $QemuDirectory |
    Tee-Object -FilePath "$out/loop-lost.log"
& (Join-Path $PSScriptRoot 'firmware.ps1') -Mode absent -Repetitions 2 -ImageDirectory $normal `
    -QemuDirectory $QemuDirectory | Tee-Object -FilePath "$out/absent.log"
& (Join-Path $PSScriptRoot 'firmware.ps1') -CleanupRecovery -ImageDirectory $recovery `
    -QemuDirectory $QemuDirectory | Tee-Object -FilePath "$out/recovery.log"
& (Join-Path $PSScriptRoot 'firmware.ps1') -Mode lost -CleanupRecovery -ImageDirectory $recoveryLost `
    -QemuDirectory $QemuDirectory | Tee-Object -FilePath "$out/recovery-lost.log"
foreach ($source in $manifest) {
    if ((Get-FileHash $source.Path).Hash -ne $source.Hash) {throw "Source changed: $($source.Path)"}
}
Write-Output "WATCHDOG_MATRIX_PASS: 30 positive boots, 30 lost-LAPIC faults, 2 absent-HPET profiles; 20 include cleanup recovery"
Write-Output "Evidence: $out"
