param([string]$QemuDirectory = 'C:\Program Files\qemu')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'status.ps1')

# ====== percorsi emulatore e firmware ====== #

$bootRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $bootRoot 'build'
$esp = Join-Path $output 'esp'
$qemu = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$code = Join-Path $QemuDirectory 'share\edk2-x86_64-code.fd'
$template = Join-Path $QemuDirectory 'share\edk2-i386-vars.fd'
$variables = Join-Path $output 'uefi-vars.fd'
foreach ($required in @($qemu, $code, $template, (Join-Path $esp 'EFI/BOOT/BOOTX64.EFI'))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "File mancante: $required. Installare QEMU e compilare il bootloader."
    }
}

# ====== copia variabili firmware ====== #

if (!(Test-Path -LiteralPath $variables)) {
    Copy-Item -LiteralPath $template -Destination $variables
}

# ====== avvio UEFI x64 ====== #

$codePath = $code.Replace('\', '/')
$varsPath = $variables.Replace('\', '/')
$espPath = $esp.Replace('\', '/')
Write-BootSection 'avvio QEMU UEFI'
Write-BootStatus INFO 'Emulazione x64, 128 MiB di RAM, rete disabilitata.'
& $qemu -machine q35 -accel tcg -m 128 -net none `
    -drive "if=pflash,format=raw,readonly=on,file=$codePath" `
    -drive "if=pflash,format=raw,file=$varsPath" `
    -drive "format=raw,file=fat:rw:$espPath"
if ($LASTEXITCODE -ne 0) {
    Write-BootStatus FAIL "QEMU terminato con errore: $LASTEXITCODE"
    throw "QEMU terminato con errore: $LASTEXITCODE"
}
