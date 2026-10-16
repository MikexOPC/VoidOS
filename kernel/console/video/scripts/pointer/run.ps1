param([string]$QemuDirectory = 'C:/Program Files/qemu', [string]$Image,
    [switch]$Rebuild, [switch]$PrepareOnly)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../..").Path
if (!$Image) { $Image = "$project/kernel/build/cursor-transaction-final/kernel.elf" }
if ($Rebuild) {
    $Image = "$project/kernel/build/pointer-interactive-image/kernel.elf"
    & "$project/kernel/scripts/build.ps1" -Ps2Mouse -ConsoleShell -OutputDirectory (Split-Path -Parent $Image)
    if (!$?) { throw 'Compilazione puntatore fallita.' }
}
$qemu = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$code = Join-Path $QemuDirectory 'share/edk2-x86_64-code.fd'
$template = Join-Path $QemuDirectory 'share/edk2-i386-vars.fd'
$loader = "$project/arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"

# ============ verify inputs before creating the session ============

foreach ($required in @($qemu,$code,$template,$loader,$Image)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "File mancante: $required. Se manca kernel.elf, usare -Rebuild."
    }
}
$binary = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Image).Path)
$markers = [Text.Encoding]::ASCII.GetString($binary)
if ($binary.Length -lt 64 -or $binary[0] -ne 127 -or $binary[1] -ne 69 -or
    $binary[2] -ne 76 -or $binary[3] -ne 70 -or
    !$markers.Contains('KERNEL_POINTER_OPEN') -or !$markers.Contains('KERNEL_POINTER_CLEAN')) {
    throw 'Richiesta immagine ELF del profilo Ps2Mouse + ConsoleShell con puntatore.'
}

# ============ the loader must be the one that starts the kernel ============

# The host boot suite rebuilds this file as a diagnostic loader that returns to the firmware
# menu without starting the kernel; its message is the only difference visible in the text.
$loaderText = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($loader))
if ($loaderText.Contains(([char[]]'cleanup senza attivazione' -join [char]0))) {
    throw 'Il loader in build/esp e la versione diagnostica dei test e non avvia il kernel. Rigenerarlo con: arch/x86/boot/uefi/scripts/build.ps1 -BootKernel'
}

# ============ isolated ESP and fresh firmware variables ============

$session = Join-Path $project ('kernel/build/pointer-interactive-' + [guid]::NewGuid().ToString('N'))
$boot = Join-Path $session 'esp/EFI/BOOT'
New-Item -ItemType Directory -Path $boot -Force | Out-Null
Copy-Item -LiteralPath $loader -Destination "$boot/BOOTX64.EFI"
Copy-Item -LiteralPath $Image -Destination "$boot/kernel.elf"
Copy-Item -LiteralPath $template -Destination "$session/vars.fd"
[IO.File]::WriteAllText("$boot/boot-check.txt","OS boot check`r`n",[Text.Encoding]::ASCII)
$imageHash = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash
$loaderHash = (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash
if ((Get-FileHash "$boot/kernel.elf").Hash -ne $imageHash -or
    (Get-FileHash "$boot/BOOTX64.EFI").Hash -ne $loaderHash) { throw 'Copia ESP non identica.' }
$codePath = $code.Replace('\','/').Replace(',',',,')
$varsPath = "$session/vars.fd".Replace('\','/').Replace(',',',,')
$espPath = "$session/esp".Replace('\','/').Replace(',',',,')
$arguments = @('-name','MikeSW OS - cursore PS/2','-machine','q35','-cpu','max',
    '-accel','tcg','-m','128','-smp','1','-net','none','-vga','std',
    '-display','sdl,show-cursor=off','-monitor','none','-no-reboot',
    '-serial',('file:' + "$session/serial.log".Replace('\','/')),
    '-drive',"if=pflash,format=raw,readonly=on,file=$codePath",
    '-drive',"if=pflash,format=raw,file=$varsPath",
    '-drive',"format=raw,file=fat:rw:$espPath")
$prepared = [pscustomobject]@{Qemu=$qemu; Arguments=$arguments; Directory=$session;
    ImageHash=$imageHash; LoaderHash=$loaderHash; Serial="$session/serial.log"}
$prepared | ConvertTo-Json -Depth 4 | Set-Content "$session/session.json" -Encoding UTF8
if ($PrepareOnly) { return $prepared }

# ============ visible interactive QEMU session ============

Write-Host '[ INFO ] Attendi il prompt e il puntatore; clicca nella finestra per catturare il mouse.'
Write-Host '[ INFO ] Ctrl+Alt+G rilascia mouse e tastiera. Prova help, echo ciao, status.'
Write-Host '[ INFO ] Prova breve: profilo diagnostico limitato. exit oppure Esc termina la sessione input.'
Write-Host "[ INFO ] Log e immagine della sessione: $session"
& $qemu @arguments
if ($LASTEXITCODE) { throw "QEMU terminato con errore $LASTEXITCODE; vedere $session" }
