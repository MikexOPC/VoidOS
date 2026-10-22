$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../..").Path
$launcher = "$project/kernel/console/video/scripts/pointer/run.ps1"

# ============ preparation uses the tested image and isolated resources ============

$image = "$project/kernel/build/pointer-interactive-image/kernel.elf"
$loader = "$project/arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"
$prepared = & $launcher -PrepareOnly
if (!$prepared -or $prepared.ImageHash -ne (Get-FileHash $image).Hash -or
    $prepared.LoaderHash -ne (Get-FileHash $loader).Hash -or
    $prepared.ImagePath -ne $image) {
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
$second = & $launcher -Image $image -PrepareOnly
if ($prepared.Directory -eq $second.Directory) { throw 'Firmware state reused between sessions.' }

# ============ missing or wrong images are rejected before starting QEMU ============

foreach ($image in @("$project/kernel/build/no-such-pointer-image.elf",
    "$project/kernel/build/pointer-keyboard-regression/kernel.elf")) {
    $rejected = $false
    try { $null = & $launcher -Image $image -PrepareOnly } catch { $rejected = $true }
    if (!$rejected) { throw "Invalid image accepted: $image" }
}
Write-Output "PASS: pointer launcher; isolated ESP, hashes, display/CPU profile and rejected images; $($prepared.Directory)"
