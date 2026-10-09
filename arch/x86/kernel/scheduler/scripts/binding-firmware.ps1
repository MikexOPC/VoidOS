param([string]$OutputPrefix = 'D:/OS/kernel/build/scheduler-binding')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$results = @()

# ============ final positive image and consecutive boots ============

$image = "$OutputPrefix-verified"
& "$project/kernel/scripts/build.ps1" -OutputDirectory $image *> "$OutputPrefix-build.log"
if (!$?) { throw 'Final kernel build failed' }
& "$PSScriptRoot/firmware.ps1" -KernelDirectory $image -Boots 10 *> "$OutputPrefix-positive.log"
if (!$?) { throw 'Final positive boots failed' }
$results += [pscustomobject]@{Profile='positive';Boots=10;Hash=(Get-FileHash "$image/kernel.elf").Hash}
'PASS: 10 consecutive final positive boots'

# ============ isolated negative images, no retry ============

foreach ($fault in @('guard-low','guard-high','nx','retired','rollback','blocked',
    'irq-timeout','idle-timeout','idle-repeat-timeout')) {
    $image = "$OutputPrefix-$fault"
    & "$project/kernel/scripts/build.ps1" -OutputDirectory $image -TestSchedulerFault $fault *> "$OutputPrefix-$fault-build.log"
    if (!$?) { throw "Fault build failed: $fault" }
    & "$PSScriptRoot/firmware.ps1" -KernelDirectory $image -Boots 1 -Fault $fault *> "$OutputPrefix-$fault.log"
    if (!$?) { throw "Fault regression failed: $fault" }
    $results += [pscustomobject]@{Profile=$fault;Boots=1;Hash=(Get-FileHash "$image/kernel.elf").Hash}
    "PASS: $fault"
}
$results | ConvertTo-Json | Set-Content "$OutputPrefix-results.json"
