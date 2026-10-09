param([ValidateRange(1,20)][int]$Boots = 10, [switch]$Ordinary, [switch]$Controller)
$ErrorActionPreference = 'Stop'
if ($Ordinary -and $Controller) { throw 'Choose ordinary or controller profile' }
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$out = Join-Path $project ('kernel/build/ps2-decoder-firmware-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null

# ============ isolated parser image and owned boots ============

$arguments = @{OutputDirectory="$out/image"}
if ($Controller) { $arguments.TestPs2Controller=$true }
elseif (!$Ordinary) { $arguments.TestPs2Decoder=$true }
& "$project/kernel/scripts/build.ps1" @arguments *> "$out/build.log"
if (!$?) { throw "Decoder build failed: $out/build.log" }
$binary = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$out/image/kernel.elf"))
if ($Ordinary -and ($binary.Contains('KERNEL_PS2_DECODER_OK') -or $binary.Contains('PS2_DECODER_ABI_FAILED') -or
    $binary.Contains('KERNEL_PS2_CONTROLLER_SET2_OK') -or $binary.Contains('PS2_CONTROLLER_FAILED'))) {
    throw 'Decoder diagnostic linked in ordinary image'
}
& "$project/arch/x86/kernel/scheduler/scripts/firmware.ps1" -KernelDirectory "$out/image" -Boots $Boots `
    *> "$out/boots.log"
if (!$?) { throw "Decoder firmware failed: $out/boots.log" }
$match = [regex]::Match([IO.File]::ReadAllText("$out/boots.log"), 'PASS: scheduler firmware evidence (.+)')
if (!$match.Success) { throw 'Missing firmware evidence directory' }
$evidence = $match.Groups[1].Value.Trim()
foreach ($boot in 1..$Boots) {
    $serial = [IO.File]::ReadAllText("$evidence/boot-$boot/serial.log")
    if ($serial.Contains('KERNEL_PS2_DECODER_OK') -eq $Ordinary.IsPresent -or !$serial.Contains('KERNEL_READY') -or
        $serial.Contains('PS2_DECODER_ABI_FAILED')) { throw "Missing decoder success in boot $boot" }
    if ($Controller -and (!$serial.Contains('KERNEL_PS2_CONTROLLER_SET2_OK') -or
        $serial.Contains('KERNEL_PS2_CONTROLLER_ABSENT') -or $serial.Contains('PS2_CONTROLLER_FAILED'))) {
        throw "Controller negotiation unproven in boot $boot"
    }
}
@{Boots=$Boots; Ordinary=$Ordinary.IsPresent; Controller=$Controller.IsPresent;
    Image=(Get-FileHash "$out/image/kernel.elf").Hash; Evidence=$evidence} |
    ConvertTo-Json | Set-Content "$out/results.json"
"PASS: decoder firmware evidence $out"
