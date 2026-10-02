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
foreach ($fault in @('execute','unmapped','guard-low','guard-high')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestWindowFault $fault -OutputDirectory (Join-Path $output $fault)
}
foreach ($fault in @('opcode','selector','divide','stack')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestCpuFault $fault -OutputDirectory (Join-Path $output $fault)
}
Write-BootSection 'kernel autonomo dopo uscita UEFI'
$candidatePages = @{}

# ====== due quantita di RAM panic e fault NX oppure dopo unmap ====== #

foreach ($case in @(@{Name='ready-128';Variant='ready';Ram=128}, @{Name='ready-256';Variant='ready';Ram=256},
    @{Name='panic-128';Variant='panic';Ram=128}, @{Name='execute-128';Variant='execute';Ram=128},
    @{Name='unmapped-128';Variant='unmapped';Ram=128}, @{Name='guard-low-128';Variant='guard-low';Ram=128},
    @{Name='guard-high-128';Variant='guard-high';Ram=128}, @{Name='opcode-128';Variant='opcode';Ram=128},
    @{Name='selector-128';Variant='selector';Ram=128}, @{Name='divide-128';Variant='divide';Ram=128},
    @{Name='stack-128';Variant='stack';Ram=128})) {
    $scenario = Join-Path $output $case.Name
    $boot = Join-Path $scenario 'esp/EFI/BOOT'
    New-Item -ItemType Directory -Force -Path $boot | Out-Null
    Copy-Item -LiteralPath $image -Destination (Join-Path $boot 'BOOTX64.EFI')
    Copy-Item -LiteralPath (Join-Path $output ($case.Variant + '/kernel.elf')) -Destination (Join-Path $boot 'kernel.elf')
    [IO.File]::WriteAllText((Join-Path $boot 'boot-check.txt'), "OS boot check`r`n", [Text.Encoding]::ASCII)
    $vars = Join-Path $scenario 'vars.fd'
    Copy-Item -LiteralPath (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') -Destination $vars
    $serial = Join-Path $scenario 'serial.log'
    $cpuLog = Join-Path $scenario 'qemu.log'
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m',[string]$case.Ram,'-net','none',
        '-d','int,cpu_reset','-D',$cpuLog.Replace('\','/'),
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
        $faultCase = $case.Variant -in @('execute','unmapped','guard-low','guard-high','opcode','selector','divide','stack')
        $expected = if ($faultCase) { 'KERNEL_EXCEPTION_HALTED' } elseif ($case.Variant -eq 'panic') { 'INTENTIONAL_TEST_PANIC' } else { 'KERNEL_READY' }
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
        $vm.StandardInput.WriteLine('x /8gx $rsp')
        if ($faultCase) {
            $frame = [regex]::Match($log, '(?i)FAULT_FRAME: 0x([0-9a-f]{16})')
            if (!$frame.Success) { throw 'Frame eccezione assente.' }
            $vm.StandardInput.WriteLine('x /7gx 0x' + $frame.Groups[1].Value)
        }
        $vm.StandardInput.WriteLine('xp /2gx 0x' + $exit.Groups[1].Value)
        $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor non risponde.' }
        $registers = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $scenario 'registers.log'), $registers)
        [IO.File]::WriteAllText((Join-Path $scenario 'stderr.log'), $stderr.GetAwaiter().GetResult())
        if (!(Test-Path -LiteralPath $cpuLog) -or (Get-Content -LiteralPath $cpuLog -Raw) -match '(?i)triple fault') {
            throw "Trace CPU assente o triple fault: $scenario"
        }
        $root = [regex]::Match($log, 'ACTIVATION_ROOT: 0x([0-9A-F]{16})').Groups[1].Value
        $marker = if ($case.Variant -eq 'panic') { '4b50414e49430001' } else { '4b52454144590001' }
        $exitPattern = '(?im)^0*' + $exit.Groups[1].Value.TrimStart('0') + ':\s+0x0000000[1-3]00000002\s+0x0000000000000000'
        if ($registers -notmatch $exitPattern -or
            $registers -notmatch ('(?i)CR3=' + $root) -or $registers -notmatch 'HLT=1' -or
            $registers -notmatch 'CS =0008' -or
            !$log.Contains('KERNEL_CONTRACT_OK') -or !$log.Contains('KERNEL_MEMORY_OK') -or
            !$log.Contains('KERNEL_PHYSICAL_OK') -or !$log.Contains('KERNEL_MAPPING_OK') -or
            !$log.Contains('KERNEL_WINDOW_16_PAGES_OK') -or !$log.Contains('KERNEL_OBJECTS_OK') -or !$log.Contains('KERNEL_IDT_OK')) {
            throw "Stato CPU o inizializzazione kernel errati: $scenario"
        }
        $idt = [regex]::Match($log, '(?i)KERNEL_IDT_BASE: 0x([0-9a-f]{16})')
        if (!$idt.Success -or $registers -notmatch ('(?i)IDT=\s*' + $idt.Groups[1].Value + '\s+000001ff')) {
            throw 'IDTR attivo diverso dalla tabella kernel verificata.'
        }
        $idtBase = [Convert]::ToUInt64($idt.Groups[1].Value,16)
        if ($idtBase -lt 0x100000000 -or $idtBase -ge 0x100100000 -or ($idtBase -band 15)) { throw 'IDT fuori dal kernel o non allineata.' }
        if (!$faultCase) {
            if ($registers -notmatch ('(?i)RAX=' + $marker) -or $registers -notmatch 'SS =0010' -or
                $registers -notmatch 'RSP=00007000000[01][0-9a-fA-F]{4}') { throw 'Arresto kernel errato.' }
        } else {
            # ====== campi numerici obbligatori senza valori impliciti ===== #

            function Read-Hex([string]$Text, [string]$Pattern) {
                $match = [regex]::Match($Text, $Pattern)
                if (!$match.Success) { throw "Campo assente: $Pattern" }
                return [Convert]::ToUInt64($match.Groups[1].Value,16)
            }
            $rsp = Read-Hex $registers 'RSP=([0-9a-f]{16})'
            $dataBase = Read-Hex $log 'ACTIVATION_DATA: 0x([0-9A-F]{16})'
            $errorCode = switch ($case.Variant) { 'execute' {17} 'selector' {40} 'stack' {2} default {0} }
            $vector = switch ($case.Variant) { 'opcode' {6} 'selector' {13} 'divide' {0} default {14} }
            $faultAddress = switch ($case.Variant) {
                'guard-low' { [Convert]::ToUInt64('70000001f000',16) }
                'guard-high' { [Convert]::ToUInt64('700000030000',16) }
                'stack' { [Convert]::ToUInt64('700000000ff8',16) }
                default { [Convert]::ToUInt64('70000002f000',16) }
            }
            $frameAddress = Read-Hex $log '(?i)FAULT_FRAME: 0x([0-9a-f]{16})'
            $faultRip = Read-Hex $log '(?i)FAULT_RIP: 0x([0-9a-f]{16})'
            $faultRsp = Read-Hex $log '(?i)FAULT_RSP: 0x([0-9a-f]{16})'
            $faultFlags = Read-Hex $log '(?i)FAULT_FLAGS: 0x([0-9a-f]{16})'
            if ((Read-Hex $registers 'RAX=([0-9a-f]{16})') -ne [Convert]::ToUInt64('4b4641554c540001',16) -or
                (Read-Hex $registers 'RIP=([0-9a-f]{16})') -lt 0x100000000 -or
                (Read-Hex $registers 'RIP=([0-9a-f]{16})') -ge 0x100100000 -or
                (Read-Hex $log '(?i)FAULT_VECTOR: 0x([0-9a-f]{16})') -ne $vector -or
                (Read-Hex $log '(?i)FAULT_ERROR: 0x([0-9a-f]{16})') -ne $errorCode -or
                (Read-Hex $log '(?i)FAULT_CS: 0x([0-9a-f]{16})') -ne 8 -or
                (Read-Hex $log '(?i)FAULT_SS: 0x([0-9a-f]{16})') -ne 16 -or
                $faultRsp -lt 0x700000001000 -or $faultRsp -ge 0x700000011000 -or
                ($faultFlags -band 512) -ne 0 -or
                $frameAddress -ne $dataBase + 11*4096 - 56 -or
                $registers -notmatch 'SS =0000' -or $rsp -lt $dataBase + 7*4096 -or $rsp -ge $dataBase + 11*4096 -or
                $registers -notmatch ('(?im)^' + $frameAddress.ToString('x16') + ':\s+0x' + $vector.ToString('x16') + '\s+0x' + $errorCode.ToString('x16')) -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 16).ToString('x16') + ':\s+0x' + $faultRip.ToString('x16') + '\s+0x0000000000000008') -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 32).ToString('x16') + ':\s+0x' + $faultFlags.ToString('x16') + '\s+0x' + $faultRsp.ToString('x16')) -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 48).ToString('x16') + ':\s+0x0000000000000010') -or
                $log.Contains('KERNEL_READY') -or $log.Contains('EXCEPTION_FRAME_INVALID') -or
                $log.Contains('EXPECTED_CPU_FAULT_MISSING') -or $log.Contains('EXPECTED_WINDOW_FAULT_MISSING')) { throw "Eccezione kernel errata: $scenario" }
            if ($vector -eq 14 -and ((Read-Hex $registers 'CR2=([0-9a-f]{16})') -ne $faultAddress -or
                (Read-Hex $log '(?i)FAULT_CR2: 0x([0-9a-f]{16})') -ne $faultAddress -or
                ($case.Variant -eq 'execute' -and $faultRip -ne $faultAddress))) { throw 'Diagnostica CR2 o RIP NX errata.' }
            if ($vector -ne 14 -and ($faultRip -lt 0x100000000 -or $faultRip -ge 0x100100000)) { throw 'RIP fault fuori dal kernel.' }
            if ($raw -notmatch '\x1b\[31m\[ FAIL \]\x1b\[0m KERNEL_EXCEPTION\r?\n') { throw 'Colore etichetta eccezione errato.' }
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
        Write-BootStatus OK "$($case.Name): mappatura, inventario e stato CPU terminale verificati."
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        $vm.Dispose()
    }
}
if ($candidatePages['ready-256'] -le $candidatePages['ready-128']) { throw 'RAM aggiuntiva non riconosciuta.' }
Write-BootStatus INFO "Log: $output"
Write-BootStatus OK '11/11 scenari: IDT kernel, frame PF UD GP DE, fault stack con IST, pool, NX, unmap e guardie verificati.'
