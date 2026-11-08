param([string]$QemuDirectory = 'C:\Program Files\qemu', [string]$ToolchainBin)
. (Join-Path $PSScriptRoot '../../../../../../scripts/lib/layout.ps1')
$ErrorActionPreference = 'Stop'
$bootRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../../arch/x86/boot/uefi')).Path
. (Join-Path $bootRoot 'scripts/status.ps1')
if (!$ToolchainBin) {
    $version = Get-ChildItem -LiteralPath 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin\Hostx64\x64'
}
$output = Join-Path $bootRoot 'build'
$image = Join-Path $output 'esp/EFI/BOOT/BOOTX64.EFI'
$qemu = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$code = Join-Path $QemuDirectory 'share\edk2-x86_64-code.fd'
$variablesTemplate = Join-Path $QemuDirectory 'share\edk2-i386-vars.fd'
$testRoot = Join-Path $output ('kernel-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Write-BootSection 'kernel reale dopo uscita UEFI e cambio CR3'

# ====== ogni kernel di test viene caricato dal percorso ELF ordinario ====== #

foreach ($fault in @('none', 'contract-write', 'stack-guard', 'execute-data')) {
    $scenario = Join-Path $testRoot $fault
    $esp = Join-Path $scenario 'esp'
    $bootDirectory = Join-Path $esp 'EFI\BOOT'
    New-Item -ItemType Directory -Path $bootDirectory | Out-Null
    Copy-Item -LiteralPath $image -Destination (Join-Path $bootDirectory 'BOOTX64.EFI')
    [IO.File]::WriteAllText((Join-Path $bootDirectory 'boot-check.txt'), "OS boot check`r`n", [Text.Encoding]::ASCII)
    & (Join-Path $bootRoot 'scripts/build-kernel-probe.ps1') -ToolchainBin $ToolchainBin -OutputDirectory $scenario -Fault $fault
    Copy-Item -LiteralPath (Join-Path $scenario 'kernel_probe.elf') -Destination (Join-Path $bootDirectory 'kernel.elf')
    $variables = Join-Path $scenario 'vars.fd'
    Copy-Item -LiteralPath $variablesTemplate -Destination $variables
    $serial = Join-Path $scenario 'serial.log'
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m','128','-net','none','-display','none',
        '-monitor','stdio','-no-reboot','-serial',('file:' + $serial.Replace('\','/')),
        '-drive',('if=pflash,format=raw,readonly=on,file=' + $code.Replace('\','/')),
        '-drive',('if=pflash,format=raw,file=' + $variables.Replace('\','/')),
        '-drive',('format=raw,file=fat:rw:' + $esp.Replace('\','/')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $qemu
    $start.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new()
    $vm.StartInfo = $start
    $started = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU non avviato' }
        $stdout = $vm.StandardOutput.ReadToEndAsync()
        $stderr = $vm.StandardError.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        $ready = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $serial) {
                $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
                $log = [regex]::Replace($log, '\x1b\[[0-?]*[ -/]*[@-~]', '')
                if ($log.Contains('KERNEL_HANDOFF_READY')) { $ready = $true; break }
            }
            if ($vm.HasExited) { throw "QEMU terminato prima del passaggio: $scenario" }
            Start-Sleep -Milliseconds 100
        }
        if (!$ready) { throw "Preflight kernel non raggiunto: $scenario" }
        Start-Sleep -Milliseconds 700
        $vm.StandardInput.WriteLine('stop')
        $vm.StandardInput.WriteLine('info registers')
        $vm.StandardInput.WriteLine('x /8gx $rsp')
        $exitAddress = [regex]::Match($log, 'ACTIVATION_EXIT_STATE: 0x([0-9A-F]{16})')
        if (!$exitAddress.Success) { throw 'Stato ExitBootServices non pubblicato' }
        $vm.StandardInput.WriteLine('xp /2gx 0x' + $exitAddress.Groups[1].Value)
        $vm.StandardInput.WriteLine('quit')
        $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor QEMU non risponde' }
        $registers = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $scenario 'registers.log'), $registers)
        [IO.File]::WriteAllText((Join-Path $scenario 'stderr.log'), $stderr.GetAwaiter().GetResult())
        function Read-Hex([string]$Text, [string]$Pattern) {
            $match = [regex]::Match($Text, $Pattern)
            if (!$match.Success) { throw "Campo assente: $Pattern" }
            return [Convert]::ToUInt64($match.Groups[1].Value,16)
        }
        $root = Read-Hex $log 'ACTIVATION_ROOT: 0x([0-9A-F]{16})'
        $base = Read-Hex $log 'ACTIVATION_CODE: 0x([0-9A-F]{16})'
        $data = Read-Hex $log 'ACTIVATION_DATA: 0x([0-9A-F]{16})'
        $rax = Read-Hex $registers 'RAX=([0-9A-Fa-f]{16})'
        $rsp = Read-Hex $registers 'RSP=([0-9A-Fa-f]{16})'
        $rip = Read-Hex $registers 'RIP=([0-9A-Fa-f]{16})'
        $exitPattern = '(?im)^0*' + $exitAddress.Groups[1].Value.TrimStart('0') + ':\s+0x([0-9A-Fa-f]{16})\s+0x0000000000000000'
        $exitWord = Read-Hex $registers $exitPattern
        if (($exitWord -band 0xffffffffL) -ne 2 -or ($exitWord -shr 32) -lt 1 -or ($exitWord -shr 32) -gt 3) {
            throw 'ExitBootServices non confermato con successo'
        }
        if ((Read-Hex $registers 'CR3=([0-9A-Fa-f]{16})') -ne $root -or
            ((Read-Hex $registers 'CR0=([0-9A-Fa-f]{8,16})') -band 65536) -eq 0 -or
            ((Read-Hex $registers 'EFER=([0-9A-Fa-f]{16})') -band 2048) -eq 0 -or
            ((Read-Hex $registers 'CR4=([0-9A-Fa-f]{8,16})') -band 128) -ne 0 -or
            ((Read-Hex $registers 'RFL=([0-9A-Fa-f]{8})') -band 1536) -ne 0 -or
            $registers -notmatch 'HLT=1' -or $registers -notmatch 'CS =0008' -or
            $registers -notmatch 'TR =0018' -or $registers -notmatch 'GDT=\s+[0-9A-Fa-f]+\s+00000037' -or
            $registers -notmatch 'IDT=\s+[0-9A-Fa-f]+\s+000001ff') {
            throw "Ambiente kernel non attivo: $scenario"
        }
        if ($fault -eq 'none') {
            if ($registers -notmatch 'SS =0010' -or $rax -ne [Convert]::ToUInt64('31454e4f444f534f',16) -or $rip -ne $base + 829 -or
                $rsp -lt ($OsWindows + 0x1000) -or $rsp -ge ($OsWindows + 0x11000)) {
                throw "Kernel ELF non eseguito correttamente: $scenario"
            }
        } else {
            $expectedAddress = switch ($fault) {
                'contract-write' { [Convert]::ToUInt64('600000000000',16) }
                'stack-guard' { ($OsWindows + 0x0) }
                'execute-data' { ($OsWindows + 0x1000) }
            }
            $expectedError = switch ($fault) { 'contract-write' {3} 'stack-guard' {0} 'execute-data' {17} }
            # IST forces SS to null; the CPU frame preserves the interrupted SS=16.
            $frameAddress = ($rsp + 32).ToString('x16')
            if ($registers -notmatch 'SS =0000' -or
                (Read-Hex $registers ('(?im)^' + $frameAddress + ':\s+0x[0-9a-f]{16}\s+0x([0-9a-f]{16})')) -ne 16 -or
                $rax -ne 14 -or $rip -ne $base + 578 -or
                (Read-Hex $registers 'CR2=([0-9A-Fa-f]{16})') -ne $expectedAddress -or
                $rsp -lt $data + 7*4096 -or $rsp -ge $data + 11*4096 -or
                (Read-Hex $registers '(?m)^[0-9a-fA-F]{16}:\s+0x([0-9a-fA-F]{16})') -ne $expectedError) {
                throw "Protezione memoria non confermata: $scenario"
            }
        }
        Write-BootStatus OK "$fault`: kernel raggiunto dopo ExitBootServices; CR3, NXE, WP e risultato CPU verificati."
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        $vm.Dispose()
    }
}
Write-BootStatus INFO "Log: $testRoot"
Write-BootStatus OK '4/4 scenari kernel e protezioni memoria verificati.'
