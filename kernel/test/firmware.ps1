param([string]$QemuDirectory = 'C:/Program Files/qemu', [switch]$UseInstalled)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
$output = Join-Path $project ('kernel/build/firmware-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
$image = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
if ($UseInstalled) {
    New-Item -ItemType Directory -Path (Join-Path $output 'ready') | Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/kernel.elf') `
        -Destination (Join-Path $output 'ready/kernel.elf')
} else {
    & (Join-Path $project 'kernel/scripts/build.ps1') -OutputDirectory (Join-Path $output 'ready')
}
& (Join-Path $project 'kernel/scripts/build.ps1') -TestPanic -OutputDirectory (Join-Path $output 'panic')
Write-BootSection 'kernel autonomo dopo uscita UEFI'
$candidatePages = @{}

# ====== due quantita di RAM e panic intenzionale senza ritorno al firmware ====== #

foreach ($case in @(@{Name='ready-128';Variant='ready';Ram=128}, @{Name='ready-256';Variant='ready';Ram=256},
    @{Name='panic-128';Variant='panic';Ram=128})) {
    $scenario = Join-Path $output $case.Name
    $boot = Join-Path $scenario 'esp/EFI/BOOT'
    New-Item -ItemType Directory -Force -Path $boot | Out-Null
    Copy-Item -LiteralPath $image -Destination (Join-Path $boot 'BOOTX64.EFI')
    Copy-Item -LiteralPath (Join-Path $output ($case.Variant + '/kernel.elf')) -Destination (Join-Path $boot 'kernel.elf')
    [IO.File]::WriteAllText((Join-Path $boot 'boot-check.txt'), "OS boot check`r`n", [Text.Encoding]::ASCII)
    $vars = Join-Path $scenario 'vars.fd'
    Copy-Item -LiteralPath (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') -Destination $vars
    $serial = Join-Path $scenario 'serial.log'
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m',[string]$case.Ram,'-net','none',
        '-display','none','-monitor','stdio','-no-reboot','-serial',('file:' + $serial.Replace('\','/')),
        '-drive',('if=pflash,format=raw,readonly=on,file=' + (Join-Path $QemuDirectory 'share/edk2-x86_64-code.fd').Replace('\','/')),
        '-drive',('if=pflash,format=raw,file=' + $vars.Replace('\','/')),
        '-drive',('format=raw,file=fat:rw:' + (Join-Path $scenario 'esp').Replace('\','/')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
    $start.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new(); $vm.StartInfo = $start
    $started = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU non avviato.' }
        $stdout = $vm.StandardOutput.ReadToEndAsync(); $stderr = $vm.StandardError.ReadToEndAsync()
        $expected = if ($case.Variant -eq 'panic') { 'INTENTIONAL_TEST_PANIC' } else { 'KERNEL_READY' }
        $deadline = [DateTime]::UtcNow.AddSeconds(40)
        $found = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $serial) {
                $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $raw = $reader.ReadToEnd() } finally { $reader.Dispose() }
                $log = [regex]::Replace($raw, '\x1b\[[0-?]*[ -/]*[@-~]', '')
                if ($log.Contains($expected)) { $found = $true; break }
            }
            if ($vm.HasExited) { throw 'QEMU terminato prima del kernel.' }
            Start-Sleep -Milliseconds 100
        }
        if (!$found) { throw "Messaggio kernel assente: $scenario" }
        Start-Sleep -Milliseconds 300
        $exit = [regex]::Match($log, 'ACTIVATION_EXIT_STATE: 0x([0-9A-F]{16})')
        if (!$exit.Success) { throw 'Indirizzo dello stato UEFI assente.' }
        $vm.StandardInput.WriteLine('stop'); $vm.StandardInput.WriteLine('info registers')
        $vm.StandardInput.WriteLine('xp /2gx 0x' + $exit.Groups[1].Value)
        $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor non risponde.' }
        $registers = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $scenario 'registers.log'), $registers)
        [IO.File]::WriteAllText((Join-Path $scenario 'stderr.log'), $stderr.GetAwaiter().GetResult())
        $root = [regex]::Match($log, 'ACTIVATION_ROOT: 0x([0-9A-F]{16})').Groups[1].Value
        $marker = if ($case.Variant -eq 'panic') { '4b50414e49430001' } else { '4b52454144590001' }
        $exitPattern = '(?im)^0*' + $exit.Groups[1].Value.TrimStart('0') + ':\s+0x0000000[1-3]00000002\s+0x0000000000000000'
        if ($registers -notmatch $exitPattern -or $registers -notmatch ('(?i)RAX=' + $marker) -or
            $registers -notmatch ('(?i)CR3=' + $root) -or $registers -notmatch 'HLT=1' -or
            $registers -notmatch 'CS =0008' -or $registers -notmatch 'SS =0010' -or
            $registers -notmatch 'RSP=00007000000[01][0-9a-fA-F]{4}' -or
            !$log.Contains('KERNEL_CONTRACT_OK') -or !$log.Contains('KERNEL_MEMORY_OK') -or !$log.Contains('KERNEL_PHYSICAL_OK')) {
            throw "Stato CPU o inizializzazione kernel errati: $scenario"
        }
        $pages = [regex]::Match($log, 'Pagine convenzionali candidate: 0x([0-9a-f]{16})')
        if (!$pages.Success -or [Convert]::ToUInt64($pages.Groups[1].Value, 16) -eq 0) { throw 'Inventario RAM vuoto.' }
        $candidatePages[$case.Name] = [Convert]::ToUInt64($pages.Groups[1].Value, 16)
        $managed = [regex]::Match($log, 'Pagine fisiche gestite: 0x([0-9a-f]{16})')
        if (!$managed.Success -or [Convert]::ToUInt64($managed.Groups[1].Value, 16) -eq 0 -or
            [Convert]::ToUInt64($managed.Groups[1].Value, 16) -gt $candidatePages[$case.Name]) { throw 'Contabilita fisica errata.' }
        $tail = $raw.Substring($raw.IndexOf('# ============ ingresso kernel x86'))
        if ($tail -notmatch '\x1b\[32m\[  OK  \]\x1b\[0m KERNEL_CONTRACT_OK' -or
            ($case.Variant -eq 'panic' -and ($log.Contains('KERNEL_READY') -or
                $tail -notmatch '\x1b\[31m\[ FAIL \]\x1b\[0m INTENTIONAL_TEST_PANIC'))) {
            throw 'Etichette o percorso panic errati.'
        }
        Write-BootStatus OK "$($case.Name): messaggi autonomi, inventario e arresto kernel verificati."
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        $vm.Dispose()
    }
}
if ($candidatePages['ready-256'] -le $candidatePages['ready-128']) { throw 'RAM aggiuntiva non riconosciuta.' }
Write-BootStatus INFO "Log: $output"
Write-BootStatus OK '3/3 scenari del kernel autonomo superati.'
