$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../..").Path
$launcher = "$project/kernel/console/video/scripts/pointer/run.ps1"

# ============ preparation uses the tested image and isolated resources ============

$prepared = & $launcher -PrepareOnly
if (!$prepared -or $prepared.ImageHash -ne 'C7B4C4896421D1D3905E97A300135BFEC54AE46402165E58311B3BA287F19100' -or
    $prepared.LoaderHash -ne '2D87ABD8C7CAD8D0C2214259C0FA9479ED75D8A259AFFD28A9AF1738765C2A02') {
    throw 'Launcher does not use the tested image/loader.'
}
if ($prepared.Arguments -notcontains 'sdl,show-cursor=off' -or
    $prepared.Arguments -notcontains 'q35' -or $prepared.Arguments -contains '-usbdevice' -or
    $prepared.Arguments -contains '-enable-kvm' -or $prepared.Arguments -contains '-nographic') {
    throw 'Unexpected interactive hardware/display profile.'
}
if (!(Test-Path "$($prepared.Directory)/esp/EFI/BOOT/kernel.elf") -or
    !(Test-Path "$($prepared.Directory)/session.json") -or
    (Get-FileHash "$($prepared.Directory)/vars.fd").Hash -ne
    (Get-FileHash 'C:/Program Files/qemu/share/edk2-i386-vars.fd').Hash) {
    throw 'Isolated ESP/firmware preparation failed.'
}
$second = & $launcher -PrepareOnly
if ($prepared.Directory -eq $second.Directory) { throw 'Firmware state reused between sessions.' }

# ============ missing or wrong images are rejected before starting QEMU ============

foreach ($image in @("$project/kernel/build/no-such-pointer-image.elf",
    "$project/kernel/build/pointer-keyboard-regression/kernel.elf")) {
    $rejected = $false
    try { $null = & $launcher -Image $image -PrepareOnly } catch { $rejected = $true }
    if (!$rejected) { throw "Invalid image accepted: $image" }
}
Write-Output "PASS: pointer launcher; isolated ESP, hashes, display/CPU profile and rejected images; $($prepared.Directory)"
