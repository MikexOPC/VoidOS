param([string]$QemuDirectory='C:/Program Files/qemu')
$ErrorActionPreference='Stop'
$module=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project=(Resolve-Path (Join-Path $module '../../../../../..')).Path
$out=Join-Path $project ('kernel/build/watchdog-compatibility-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$sources=@(Get-ChildItem $module -Recurse -File | Where-Object Extension -In '.c','.h','.rs','.ps1') +
    @(Get-Item (Join-Path $project 'kernel/scripts/build.ps1'))
$manifest=$sources | ForEach-Object {Get-FileHash $_.FullName}
$manifest | ConvertTo-Json | Out-File "$out/sources.json" -Encoding utf8
$normal=Join-Path $out 'alternate'
$lost=Join-Path $out 'alternate-lost'

# ============ isolated policy reservation, real comparator 1 ============

& (Join-Path $project 'kernel/scripts/build.ps1') -TestWatchdogAlternate -OutputDirectory $normal |
    Out-File "$out/build-alternate.log"
& (Join-Path $project 'kernel/scripts/build.ps1') -TestWatchdogAlternate -TestWatchdogLostLapic `
    -OutputDirectory $lost | Out-File "$out/build-alternate-lost.log"
Get-FileHash "$normal/kernel.elf","$lost/kernel.elf" |
    ConvertTo-Json | Out-File "$out/images.json" -Encoding utf8
& (Join-Path $PSScriptRoot 'firmware.ps1') -ImageDirectory $normal -ExpectedTimer 1 `
    -QemuDirectory $QemuDirectory | Tee-Object -FilePath "$out/alternate.log"
& (Join-Path $PSScriptRoot 'firmware.ps1') -Mode lost -ImageDirectory $lost -ExpectedTimer 1 `
    -QemuDirectory $QemuDirectory | Tee-Object -FilePath "$out/alternate-lost.log"
foreach ($source in $manifest) {
    if ((Get-FileHash $source.Path).Hash -ne $source.Hash) {throw "Source changed: $($source.Path)"}
}
Write-Output 'WATCHDOG_COMPATIBILITY_PASS: 10 alternate positive boots, 10 alternate lost-LAPIC profiles'
Write-Output "Evidence: $out"
