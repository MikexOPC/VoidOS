param([ValidateRange(1,20)][int]$Boots = 10)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$out = Join-Path $project ('kernel/build/bootstrap-firmware-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$results = @()
$diagnosticMarkers = @('KERNEL_PHYSICAL_ALIGNED_OK','KERNEL_MAPPING_OK',
    'KERNEL_WINDOW_16_PAGES_OK','KERNEL_OBJECTS_OK','KERNEL_BOOTSTRAP_DIAGNOSTICS_OK')

# ============ final images and consecutive owned boots ============

foreach ($bootProfile in @('ordinary','diagnostic')) {
    $image = "$out/$bootProfile"
    $arguments = @{OutputDirectory=$image}
    if ($bootProfile -eq 'diagnostic') { $arguments.TestBootstrapMemory=$true }
    & "$project/kernel/scripts/build.ps1" @arguments *> "$out/$bootProfile-build.log"
    if (!$?) { throw "Build failed: $bootProfile" }
    $binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$image/kernel.elf"))
    if ($bootProfile -eq 'ordinary') {
        foreach ($marker in $diagnosticMarkers + @('PAGE_REUSE_NOT_ZERO','OBJECT_NEIGHBOR_CORRUPTED',
            'REGION_REUSE_NOT_ZERO','PERSISTENT_OBJECT_NOT_ZERO')) {
            if ($binaryText.Contains($marker)) { throw "Diagnostic string linked in ordinary image: $marker" }
        }
    } elseif (!$binaryText.Contains('KERNEL_BOOTSTRAP_DIAGNOSTICS_OK')) { throw 'Missing diagnostic code' }
    & "$project/arch/x86/kernel/scheduler/scripts/firmware.ps1" -KernelDirectory $image -Boots $Boots `
        *> "$out/$bootProfile-boots.log"
    if (!$?) { throw "Firmware failed: $bootProfile" }
    $match = [regex]::Match([IO.File]::ReadAllText("$out/$bootProfile-boots.log"),
        'PASS: scheduler firmware evidence (.+)')
    if (!$match.Success) { throw 'Missing CPU/memory evidence directory' }
    $evidence = $match.Groups[1].Value.Trim()
    foreach ($boot in 1..$Boots) {
        $serial = [IO.File]::ReadAllText("$evidence/boot-$boot/serial.log")
        foreach ($marker in @('KERNEL_PHYSICAL_OK','KERNEL_REGIONS_OK','KERNEL_ARENA_OK',
            'KERNEL_PERSISTENT_STATE_OK','KERNEL_PAGING_INVENTORY_OK','KERNEL_READY')) {
            if (!$serial.Contains($marker)) { throw "Missing bootstrap marker: $marker" }
        }
        if ($bootProfile -eq 'ordinary') {
            if (!$serial.Contains('KERNEL_BOOTSTRAP_ORDINARY_OK') -or !$serial.Contains('KERNEL_WINDOW_READY')) {
                throw 'Ordinary initialization not proven'
            }
            foreach ($marker in $diagnosticMarkers) {
                if ($serial.Contains($marker)) { throw "Unexpected diagnostic execution: $marker" }
            }
        } else {
            foreach ($marker in $diagnosticMarkers) {
                if (!$serial.Contains($marker)) { throw "Missing diagnostic execution: $marker" }
            }
            if ($serial.Contains('KERNEL_BOOTSTRAP_ORDINARY_OK')) { throw 'Ambiguous profile' }
        }
    }
    $results += [pscustomobject]@{Profile=$bootProfile;Boots=$Boots;Evidence=$evidence;
        Image="$image/kernel.elf";Hash=(Get-FileHash "$image/kernel.elf").Hash}
    "PASS: $bootProfile $Boots consecutive boots; profile exclusion and CPU/memory verified"
}
$results | ConvertTo-Json | Set-Content "$out/results.json"
"PASS: bootstrap firmware evidence $out"
