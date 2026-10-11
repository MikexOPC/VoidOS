# ============ owner timer e registri xAPIC dopo arresto e ripristino ============ #

function Write-LapicTimerMonitor($log, $vm) {
    $owner = Read-Hex $log 'KERNEL_TIMER_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /18gx 0x'+$owner.ToString('x16'))
    $resume = Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /3bx 0x'+($resume-2).ToString('x16'))
    # Il monitor QEMU espone i registri APIC tramite info lapic, non x MMIO.
}

# ============ qword letta dal monitor senza fidarsi dei soli messaggi OK ============ #

function Read-TimerWord($registers, [uint64]$address) {
    $row = $address -band -16L
    $match = [regex]::Match($registers,'(?im)^'+$row.ToString('x16')+':\s+0x([0-9a-f]{16})(?:\s+0x([0-9a-f]{16}))?')
    if (!$match.Success) { throw 'Owner timer incompleto nel monitor.' }
    return [Convert]::ToUInt64($match.Groups[1+([int](($address -band 8)/8))].Value,16)
}

# ============ alterazioni degli stati live e del ripristino devono essere rifiutate ============ #

function Test-LapicTimerMonitorRejections($log, $registers, $cpuLog) {
    Test-LapicTimerFirmware $log $registers $cpuLog
    $owner = Read-Hex $log 'KERNEL_TIMER_OWNER: 0x([0-9a-f]{16})'
    foreach ($offset in @(0,8,24,32,72,80,88,96,112,120,128,136)) {
        $address = ($owner+($offset -band -16)).ToString('x16')
        $row = [regex]::Match($registers,'(?im)^'+$address+':\s+0x([0-9a-f]{16})(?:\s+0x([0-9a-f]{16}))?')
        if (!$row.Success) { throw 'Fixture timer incompleta.' }
        $left = [Convert]::ToUInt64($row.Groups[1].Value,16)
        $right = if ($row.Groups[2].Success) { [Convert]::ToUInt64($row.Groups[2].Value,16) } else { 0 }
        $bit = if ($offset -in @(24,72,112)) { 4294967296L } else { 1 }
        if ($offset -band 8) { $right = $right -bxor $bit } else { $left = $left -bxor $bit }
        $changed = $address+': 0x'+$left.ToString('x16')
        if ($row.Groups[2].Success) { $changed += ' 0x'+$right.ToString('x16') }
        $rejected = $false
        try { Test-LapicTimerFirmware $log ($registers.Replace($row.Value,$changed)) $cpuLog } catch { $rejected=$true }
        if (!$rejected) { throw "Corruzione timer accettata a offset $offset." }
    }
    $opcode = [regex]::Match($registers,'(?im)^00000001[0-9a-f]+:\s+0xfb\s+0xf4\s+0xfa')
    if (!$opcode.Success) { throw 'Fixture opcodes attesa assente.' }
    $rejected = $false
    try { Test-LapicTimerFirmware $log ($registers.Replace($opcode.Value,$opcode.Value.Replace('0xf4','0x90'))) $cpuLog } catch { $rejected=$true }
    if (!$rejected) { throw 'Attesa senza HLT accettata.' }
    $resume = Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME: 0x([0-9a-f]{16})'
    $rejected = $false
    try { Test-LapicTimerFirmware ($log.Replace($resume.ToString('x16'),($resume+1).ToString('x16'))) $registers $cpuLog } catch { $rejected=$true }
    if (!$rejected) { throw 'Ritorno dal sonno falso accettato.' }
    if ($log.Contains('KERNEL_SCHEDULER_TASK_IRQ_OK')) {
        $trace = [IO.File]::ReadAllText($cpuLog)
        $worker = [regex]::Match($trace,'(?s)Servicing hardware INT=0x41\s+.*?RIP=([0-9a-f]{16}) RFL=([0-9a-f]+)')
        if (!$worker.Success) { throw 'Fixture worker IRQ assente.' }
        $rip = [Convert]::ToUInt64($worker.Groups[1].Value,16)
        $flags = [Convert]::ToUInt64($worker.Groups[2].Value,16)
        $mutations = @($worker.Value.Replace('RIP='+$worker.Groups[1].Value,'RIP='+($rip+1).ToString('x16')),
            $worker.Value.Replace('RFL='+$worker.Groups[2].Value,'RFL='+($flags -bxor 512).ToString('x')))
        $temporary = [IO.Path]::GetTempFileName()
        try {
            foreach ($mutation in $mutations) {
                [IO.File]::WriteAllText($temporary,$trace.Replace($worker.Value,$mutation))
                $rejected = $false
                try { Test-LapicTimerFirmware $log $registers $temporary } catch { $rejected=$true }
                if (!$rejected) { throw 'Frame worker IRQ falso accettato.' }
            }
        } finally { Remove-Item -LiteralPath $temporary }
        Write-Output 'Timer LAPIC: due alterazioni worker RIP/IF rifiutate.'
    }
    Write-Output 'Timer LAPIC: fotografia reale valida e 14 alterazioni base rifiutate.'
}

# ============ due conversioni ricalcolate dai campioni raw ============ #

function Test-LapicTimerFirmware($log, $registers, $cpuLog) {
    $owner = Read-Hex $log 'KERNEL_TIMER_OWNER: 0x([0-9a-f]{16})'
    $mapping = Read-Hex $log 'KERNEL_LAPIC_MAPPING: 0x([0-9a-f]{16})'
    $reload = Read-Hex $log 'KERNEL_TIMER_RELOAD: 0x([0-9a-f]{16})'
    $mask = Read-Hex $log 'KERNEL_TIMER_PM_MASK: 0x([0-9a-f]{16})'
    $port = Read-Hex $log 'KERNEL_TIMER_PM_PORT: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_TIMER_OK') -or !$log.Contains('KERNEL_TIMER_EVENTS_OK') -or
        !$log.Contains('KERNEL_IRQ_SLEEP_REJECT_OK') -or ($owner -band 7) -or
        $owner -lt 0x100000000 -or $owner+144 -gt 0x100100000 -or
        !$reload -or $reload -gt 4294967295L -or $mask -notin @(16777215L,4294967295L) -or
        $port -lt 256 -or $port -gt 65532 -or ($port -band 3) -or ($port -ge 1016 -and $port -lt 1024) -or
        (Read-Hex $log 'KERNEL_TIMER_IRQ_COUNT: 0x([0-9a-f]{16})') -ne 3 -or
        (Read-Hex $log 'KERNEL_TIMER_ERROR: 0x([0-9a-f]{16})') -ne 0 -or
        (Read-Hex $log 'KERNEL_TIMER_DELIVERED: 0x([0-9a-f]{16})') -ne 3 -or
        (Read-TimerWord $registers $owner) -ne 3 -or
        (Read-TimerWord $registers ($owner+8)) -ne $mapping -or
        (Read-TimerWord $registers ($owner+72)) -ne $reload -or
        (Read-TimerWord $registers ($owner+80)) -ne 0 -or
        (Read-TimerWord $registers ($owner+88)) -ne 0 -or
        (Read-TimerWord $registers ($owner+136)) -ne 17179869187L -or
        (Read-Hex $log 'KERNEL_TIMER_CONSUMED: 0x([0-9a-f]{16})') -ne 3 -or
        (Read-Hex $log 'KERNEL_TIMER_PHASE: 0x([0-9a-f]{16})') -ne 4) { throw 'Timer: owner o consegne non conformi.' }
    $clock = Read-TimerWord $registers ($owner+24)
    $length = $clock -band 4294967295L
    if ($length -lt 116 -or $length -gt 4096 -or ($clock -shr 32) -ne $port -or
        (Read-TimerWord $registers ($owner+32)) -ne (4294967296L+$mask)) { throw 'Reference clock non conforme.' }
    $counts = @()
    for ($index=0; $index -lt 2; ++$index) {
        $sample = Read-TimerWord $registers ($owner+40+$index*16)
        $result = Read-TimerWord $registers ($owner+48+$index*16)
        $start = $sample -band 4294967295L; $end = $sample -shr 32
        $elapsed = $result -band 4294967295L; $count = $result -shr 32
        if (!$end -or $start -le $end -or $elapsed -lt 35795 -or $elapsed -gt 143180 -or
            [Math]::Ceiling(($start-$end)*35795.0/$elapsed) -ne $count) { throw 'Campione timer incoerente.' }
        $counts += $count
    }
    $low = [Math]::Min($counts[0],$counts[1]); $high = [Math]::Max($counts[0],$counts[1])
    if ($reload -ne $low -or $high-$low -gt [Math]::Floor($low/4)) { throw 'Campioni discordanti.' }
    $periods = Read-TimerWord $registers ($owner+104)
    $last = Read-TimerWord $registers ($owner+112)
    foreach ($elapsed in @(($periods -band 4294967295L),($periods -shr 32),($last -band 4294967295L))) {
        if ($elapsed -lt 17897 -or $elapsed -gt 715900) { throw 'Scadenza senza intervallo PM verificato.' }
    }
    if (($last -shr 32) -ne 0 -or [regex]::Matches((Get-Content -LiteralPath $cpuLog -Raw),'Servicing hardware INT=0x41').Count -ne 3) {
        throw 'Timer: Reserved o traccia IRQ non conformi.'
    }
    $original = Read-TimerWord $registers ($owner+96)
    $wakes = Read-TimerWord $registers ($owner+120)
    $probe = Read-TimerWord $registers ($owner+128)
    if (($wakes -band 4294967295L) -notin @(1L,2L) -or ($wakes -shr 32) -notin @(1L,2L) -or
        ($probe -band 4294967295L) -notin @(1L,2L) -or ($probe -shr 32) -ne 3 -or
        (Read-Hex $log 'KERNEL_TIMER_PENDING_PROBE: 0x([0-9a-f]{16})') -ne 3) {
        throw 'Timer: sonno o consegna gia presente non verificati.'
    }
    $resume = Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME: 0x([0-9a-f]{16})'
    if ($resume -lt 0x100000002 -or $resume -ge 0x100100000 -or
        $registers -notmatch ('(?im)^'+($resume-2).ToString('x16')+':\s+0xfb\s+0xf4\s+0xfa')) {
        throw 'Sequenza STI HLT CLI assente nell''immagine eseguita.'
    }
    $trace = Get-Content -LiteralPath $cpuLog -Raw
    $events = [regex]::Matches($trace,'(?s)Servicing hardware INT=0x41\s+.*?RIP=([0-9a-f]{16}) RFL=([0-9a-f]+)')
    $workerRip = $resume
    if ($log.Contains('KERNEL_SCHEDULER_TASK_IRQ_OK')) {
        $workerRip = Read-Hex $log 'SCHEDULER_IRQ_RIP: 0x([0-9a-f]{16})'
        if ($workerRip -lt (Read-Hex $log 'SCHEDULER_IRQ_BEGIN: 0x([0-9a-f]{16})') -or
            $workerRip -ge (Read-Hex $log 'SCHEDULER_IRQ_END: 0x([0-9a-f]{16})')) { throw 'Worker IRQ fuori dal codice verificato.' }
    }
    for ($index=0; $index -lt $events.Count; ++$index) {
        $expectedRip = if ($index -eq 0) { $workerRip } else { $resume }
        $flags = [Convert]::ToUInt64($events[$index].Groups[2].Value,16)
        if ([Convert]::ToUInt64($events[$index].Groups[1].Value,16) -ne $expectedRip -or
            !($flags -band 512) -or ($flags -band 1024)) { throw 'IRQ timer fuori dal contesto task/sonno verificato.' }
    }
    if ($registers -notmatch ('(?im)^LVTT\s+0x'+([uint32]($original -band 4294967295L)).ToString('x8')) -or
        $registers -notmatch ('(?im)^Timer\s+DCR=0x'+($original -shr 32).ToString('x')+'\s+.*initial_count = 0 current_count = 0')) {
        throw 'Timer non fermato o ripristinato.'
    }
}
