param([string]$QemuDirectory='C:/Program Files/qemu',[switch]$UseBuilt,
    [string]$ImageDirectory,[ValidateRange(8,128)][int]$StopAfterEvents=8,
    [ValidateSet(128,256)][int]$Ram=128,[ValidateSet(1,4)][int]$Processors=1)
. (Join-Path $PSScriptRoot '../../../../../scripts/lib/layout.ps1')
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
. (Join-Path $PSScriptRoot 'firmware.ps1')
. (Join-Path $project 'arch/x86/kernel/timer/hpet/watchdog/scripts/validation.ps1')
$output=Join-Path $project ('kernel/build/loop-firmware-'+[guid]::NewGuid().ToString('N'))
$boot=Join-Path $output 'esp/EFI/BOOT'
New-Item -ItemType Directory -Force -Path $boot | Out-Null
if (!$ImageDirectory) {$ImageDirectory=Join-Path $project 'kernel/build/event-loop'}
if (!$UseBuilt) { & (Join-Path $project 'kernel/scripts/build.ps1') -EventLoop -OutputDirectory $ImageDirectory }
Copy-Item (Join-Path $ImageDirectory 'kernel.elf') (Join-Path $boot 'kernel.elf')
Copy-Item (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI') (Join-Path $boot 'BOOTX64.EFI')
[IO.File]::WriteAllText((Join-Path $boot 'boot-check.txt'),"OS boot check`r`n",[Text.Encoding]::ASCII)
Copy-Item (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') (Join-Path $output 'vars.fd')
$serial=Join-Path $output 'serial.log'; $cpu=Join-Path $output 'qemu.log'
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$listener.Start(); $port=$listener.LocalEndpoint.Port; $listener.Stop()

# ============ lettura seriale condivisa senza interpretare sequenze terminale ============ #

function Read-Serial {
    if (!(Test-Path $serial)) { return '' }
    $file=[IO.File]::Open($serial,'Open','Read','ReadWrite'); $reader=[IO.StreamReader]::new($file)
    try { return [regex]::Replace($reader.ReadToEnd(),'\x1b\[[0-?]*[ -/]*[@-~]','') } finally { $reader.Dispose() }
}

# ============ campi obbligatori senza valori predefiniti ============ #

function Read-Hex([string]$Text,[string]$Pattern) {
    $match=[regex]::Match($Text,$Pattern)
    if (!$match.Success) { throw "Campo assente: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}

# ============ pacchetto GDB con checksum e risposta bounded ============ #

function Send-Gdb($stream,[string]$body) {
    $sum=0; foreach($byte in [Text.Encoding]::ASCII.GetBytes($body)) { $sum=($sum+$byte)-band 255 }
    $bytes=[Text.Encoding]::ASCII.GetBytes('$'+$body+'#'+$sum.ToString('x2'))
    $stream.Write($bytes,0,$bytes.Length)
}
function Read-Gdb($stream) {
    $value=''; $sum=0; $started=$false
    for($index=0;$index -lt 4096;++$index) {
        $byte=$stream.ReadByte(); if($byte -lt 0) { throw 'GDB disconnesso.' }
        if(!$started) { if($byte -eq 36) {$started=$true}; continue }
        if($byte -eq 35) {
            $a=$stream.ReadByte(); $b=$stream.ReadByte()
            if($a -lt 0 -or $b -lt 0 -or [Convert]::ToInt32(([char]$a).ToString()+[char]$b,16) -ne $sum) {throw 'Checksum GDB invalido.'}
            $stream.WriteByte(43); return $value
        }
        $sum=($sum+$byte)-band 255; $value += [char]$byte
    }
    throw 'Risposta GDB troppo lunga.'
}

# ============ VM nascosta con porta debug limitata al loopback ============ #

$arguments=@('-machine','q35','-cpu','max','-accel','tcg','-m',"$Ram",'-smp',"$Processors",'-net','none',
    '-d','int,cpu_reset','-D',$cpu.Replace('\','/'),'-display','none','-monitor','stdio','-no-reboot',
    '-gdb',('tcp:127.0.0.1:'+$port),'-serial',('file:'+$serial.Replace('\','/')),
    '-drive',('if=pflash,format=raw,readonly=on,file='+(Join-Path $QemuDirectory 'share/edk2-x86_64-code.fd').Replace('\','/')),
    '-drive',('if=pflash,format=raw,file='+(Join-Path $output 'vars.fd').Replace('\','/')),
    '-drive',('format=raw,file=fat:rw:'+(Join-Path $output 'esp').Replace('\','/')))
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$start.Arguments=($arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
$start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$vm=[Diagnostics.Process]::new(); $vm.StartInfo=$start; $client=$null; $started=$false
try {
    $started=$vm.Start(); if(!$started) {throw 'QEMU non avviato.'}
    $stdout=$vm.StandardOutput.ReadToEndAsync(); $stderr=$vm.StandardError.ReadToEndAsync()
    $deadline=[DateTime]::UtcNow.AddSeconds(40)
    do {
        $log=Read-Serial
        if($vm.HasExited) {throw 'QEMU terminato prima del ciclo.'}
        if($log.Contains('KERNEL_READY') -or $log.Contains('[ FAIL ]')) {throw 'Ciclo terminato prima della richiesta.'}
        $progress=[regex]::Matches($log,'KERNEL_TIMER_LOOP_EVENTS: 0x([0-9a-f]{16})')
        if($progress.Count -and [Convert]::ToUInt64($progress[$progress.Count-1].Groups[1].Value,16) -ge $StopAfterEvents) {break}
        Start-Sleep -Milliseconds 100
    } while([DateTime]::UtcNow -lt $deadline)
    if(!$log.Contains('KERNEL_TIMER_LOOP_EVENTS:')) {throw 'Ciclo non avanza oltre il bootstrap.'}
    $control=Read-Hex $log 'KERNEL_TIMER_LOOP_CONTROL: 0x([0-9a-f]{16})'
    $owner=Read-Hex $log 'KERNEL_TIMER_LOOP_OWNER: 0x([0-9a-f]{16})'
    if(($control -band 7) -or $control -lt $OsImageBase -or $control+48 -gt $OsImageLimit) {throw 'Controllo debug fuori immagine.'}
    $client=[Net.Sockets.TcpClient]::new(); $client.Connect('127.0.0.1',$port)
    $stream=$client.GetStream(); $stream.ReadTimeout=5000; $stream.WriteTimeout=5000
    Send-Gdb $stream '?'; $reply=Read-Gdb $stream
    if($reply -notmatch '^[TS]') {throw 'CPU non fermata dal debugger.'}
    Send-Gdb $stream ('m'+($control+16).ToString('x')+',4')
    if((Read-Gdb $stream) -ne '00000000') {throw 'Richiesta stop non vergine.'}
    Send-Gdb $stream ('M'+($control+16).ToString('x')+',4:01000000')
    if((Read-Gdb $stream) -ne 'OK') {throw 'Richiesta stop non scritta.'}
    Send-Gdb $stream ('m'+($control+16).ToString('x')+',4')
    if((Read-Gdb $stream) -ne '01000000') {throw 'Readback stop invalido.'}
    Send-Gdb $stream 'D'; if((Read-Gdb $stream) -ne 'OK') {throw 'Detach GDB fallito.'}
    $client.Dispose(); $client=$null
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do { $log=Read-Serial; if($log.Contains('KERNEL_READY')) {break}; Start-Sleep -Milliseconds 100 } while([DateTime]::UtcNow -lt $deadline)
    if(!$log.Contains('KERNEL_READY') -or !$log.Contains('KERNEL_TIMER_LOOP_CLOSED')) {throw 'Stop senza cleanup completo.'}
    $count=Read-Hex $log 'KERNEL_TIMER_IRQ_COUNT: 0x([0-9a-f]{16})'
    if($count -lt $StopAfterEvents -or (Read-Hex $log 'KERNEL_TIMER_CONSUMED: 0x([0-9a-f]{16})') -ne $count -or
        (Read-Hex $log 'KERNEL_TIMER_DELIVERED: 0x([0-9a-f]{16})') -ne $count -or
        (Read-Hex $log 'KERNEL_TIMER_PHASE: 0x([0-9a-f]{16})') -ne 4) {throw 'Eventi persi o non consumati.'}
    $vm.StandardInput.WriteLine('stop'); $vm.StandardInput.WriteLine('info registers'); $vm.StandardInput.WriteLine('info lapic')
    $vm.StandardInput.WriteLine('x /8gx 0x'+($control -band -16L).ToString('x16'))
    $vm.StandardInput.WriteLine('x /18gx 0x'+$owner.ToString('x16'))
    $watchdog=Read-Hex $log 'KERNEL_WATCHDOG_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /30gx 0x'+($watchdog -band -16L).ToString('x16'))
    $resume=Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /3bx 0x'+($resume-2).ToString('x16'))
    $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
    if(!$vm.WaitForExit(5000)) {throw 'Monitor non risponde.'}
    $registers=$stdout.GetAwaiter().GetResult(); [IO.File]::WriteAllText((Join-Path $output 'registers.log'),$registers)
    [IO.File]::WriteAllText((Join-Path $output 'stderr.log'),$stderr.GetAwaiter().GetResult())
    $trace=Get-Content $cpu -Raw
    if($trace -match '(?i)triple fault' -or [regex]::Matches($trace,'Servicing hardware INT=0x41').Count -ne $count -or
        $registers -notmatch 'HLT=1' -or $registers -notmatch '(?im)^ISR\s+\(none\)' -or
        $registers -notmatch '(?im)^IRR\s+\(none\)' -or
        $registers -notmatch '(?im)^LVTT\s+0x00010040' -or
        $registers -notmatch '(?im)^Timer\s+DCR=0xb\s+.*initial_count = 0 current_count = 0') {throw 'Stato terminale o traccia IRQ errati.'}
    $flags=Read-Hex $registers 'RFL=([0-9a-f]+)'
    if(($flags -band 512) -or $resume -lt ($OsImageBase + 2) -or $resume -ge $OsImageLimit -or
        $registers -notmatch ('(?im)^'+($resume-2).ToString('x16')+':\s+0xfb\s+0xf4\s+0xfa')) {throw 'IF terminale o sequenza sonno non conformi.'}
    $events=[regex]::Matches($trace,'(?s)Servicing hardware INT=0x41\s+.*?RIP=([0-9a-f]{16}) RFL=([0-9a-f]+)')
    if($events.Count -ne $count) {throw 'Trace timer incompleta.'}
    $taskIrq = $log.Contains('KERNEL_SCHEDULER_TASK_IRQ_OK')
    if ($taskIrq) {
        $taskRip=Read-Hex $log 'SCHEDULER_IRQ_RIP: 0x([0-9a-f]{16})'
        $taskBegin=Read-Hex $log 'SCHEDULER_IRQ_BEGIN: 0x([0-9a-f]{16})'
        $taskEnd=Read-Hex $log 'SCHEDULER_IRQ_END: 0x([0-9a-f]{16})'
        $taskRsp=Read-Hex $log 'SCHEDULER_IRQ_RSP: 0x([0-9a-f]{16})'
        if ($taskRip -lt $taskBegin -or $taskRip -ge $taskEnd -or
            $taskRsp -lt ($OsWindows + 0x154000) -or $taskRsp -ge ($OsWindows + 0x163000) -or
            (Read-Hex $log 'SCHEDULER_IRQ_TASK: 0x([0-9a-f]{16})') -ne 1 -or
            (Read-Hex $log 'SCHEDULER_IRQ_GENERATION: 0x([0-9a-f]{16})') -lt 2) {
            throw 'Owner o compute della prima consegna task non validi.'
        }
    }
    for ($eventIndex=0; $eventIndex -lt $events.Count; ++$eventIndex) {
        $event=$events[$eventIndex]
        $expectedRip=if ($taskIrq -and $eventIndex -eq 0) {$taskRip} else {$resume}
        if([Convert]::ToUInt64($event.Groups[1].Value,16) -ne $expectedRip -or
            !([Convert]::ToUInt64($event.Groups[2].Value,16) -band 512)) {throw 'Evento timer fuori dal contesto IF=1 previsto.'}
    }
    $consumption=Read-TimerWord $registers ($control+24)
    if(($consumption -band 4294967295L) -ne $count -or ($consumption -shr 32) -lt $count -or
        ($consumption -shr 32) -gt 2*$count) {throw 'Consumo o conteggio dei risvegli invalido.'}
    if((Read-TimerWord $registers $control) -ne $control -or (Read-TimerWord $registers ($control+8)) -ne $owner -or
        (Read-TimerWord $registers ($control+16)) -ne 8589934593L -or
        (Read-TimerWord $registers ($control+32)) -ne 1 -or
        (Read-TimerWord $registers ($control+40)) -ne 4294967297L -or
        (Read-TimerWord $registers $owner) -ne $count -or
        (Read-TimerWord $registers ($owner+80)) -ne 0 -or (Read-TimerWord $registers ($owner+88)) -ne 0 -or
        (Read-TimerWord $registers ($owner+136)) -ne ((4L -shl 32)+$count)) {throw 'Controllo ciclo o owner non chiusi.'}
    $watchdogCount=Read-Hex $log 'KERNEL_WATCHDOG_FIRED: 0x([0-9a-f]{16})'
    $watchdogResult=Read-Hex $log 'KERNEL_WATCHDOG_RESULT: 0x([0-9a-f]{16})'
    if($watchdogCount -lt $count -or $watchdogCount -gt $count+1 -or $watchdogResult -notin @(1L,4L)) {
        throw 'Evento o stop watchdog invalido.'
    }
    Test-WatchdogOwner $log $registers $watchdogCount $watchdogResult
    $wakes=[regex]::Matches($trace,'(?s)Servicing hardware INT=0x40\s+.*?RIP=([0-9a-f]{16}) RFL=([0-9a-f]+)')
    $wakes=@($wakes | Where-Object {[Convert]::ToUInt64($_.Groups[1].Value,16) -eq $resume})
    if($wakes.Count -ne $watchdogCount) {throw 'Traccia IRQ HPET incompleta.'}
    Write-BootStatus OK "Ciclo attivo: $count eventi, stop esterno e cleanup verificati."
    Write-BootStatus INFO "Log: $output"
} finally {
    if($client) {$client.Dispose()}
    if($started -and !$vm.HasExited) {$vm.Kill(); $vm.WaitForExit()}
    $vm.Dispose()
}
