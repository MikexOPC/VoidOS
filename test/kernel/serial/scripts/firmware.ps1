# ====== snapshot owner UART dopo la chiusura della sorgente ===== #

function Write-UartMonitor($log, $vm) {
    Write-IrqRegistryMonitor $log $vm
    Write-LapicTimerMonitor $log $vm
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /6gx 0x' + $owner.ToString('x16'))
    $tx = Read-Hex $log 'KERNEL_UART_TX_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /11gx 0x' + $tx.ToString('x16'))
    $queue = Read-Hex $log 'KERNEL_UART_QUEUE_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /34gx 0x' + $queue.ToString('x16'))
    $mapping = Read-Hex $log 'KERNEL_LAPIC_MAPPING: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /5gx 0x' + $mapping.ToString('x16'))
    $ist = Read-Hex $log 'KERNEL_IST_PTE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /3gx 0x' + ($ist+(287-64)*8).ToString('x16'))
    $paging = Read-Hex $log 'KERNEL_PAGING_MIXED_BASE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /6gx 0x' + ($paging+3096).ToString('x16'))
}

# ====== prova iniziale e bytes TX tramite IRQ con owner finale inattivo ===== #

function Test-UartFirmware($log, $registers, $cpuLog) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $row = [regex]::Match($registers,'(?im)^'+($owner+16).ToString('x16')+':\s+0x'+$profile.ToString('x16')+'\s+0x0000000000000000')
    $trace = Get-Content -LiteralPath $cpuLog -Raw
    Test-LapicRetention $log $registers
    Test-UartTransmit $log $registers $owner
    Test-UartQueue $log $registers
    Test-IrqRegistryFirmware $log $registers
    Test-LapicTimerFirmware $log $registers $cpuLog
    # HPET reuses registry vector 0x40 after UART is detached (watchdog/core.c).
    # Keep UART deliveries separate from the three watchdog interrupts.
    $expectedVector40 = 140
    if ($log.Contains('KERNEL_WATCHDOG_READY')) {
        if (!$log.Contains('KERNEL_WATCHDOG_CLEANUP_OK') -or
            (Read-Hex $log 'KERNEL_WATCHDOG_FIRED: 0x([0-9a-f]{16})') -ne 3) {
            throw 'Watchdog: consegne o chiusura non conformi al profilo finito.'
        }
        $expectedVector40 += 3
    }
    if (!$log.Contains('KERNEL_UART_IRQ_OK') -or ($owner -band 7) -or
        $owner -lt 0x100000000 -or $owner+48 -gt 0x100100000 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_COUNT: 0x([0-9a-f]{16})') -ne 140 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_ERROR: 0x([0-9a-f]{16})') -ne 0 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_VECTOR: 0x([0-9a-f]{16})') -ne 64 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_GSI: 0x([0-9a-f]{16})') -ne 4 -or
        !$row.Success -or $registers -notmatch ('(?im)^'+$owner.ToString('x16')+':\s+0x000000000000008c\s+0x[0-9a-f]{16}') -or
        [regex]::Matches($trace,'Servicing hardware INT=0x40').Count -ne $expectedVector40) {
        throw 'COM1: consegne hardware, contatore, EOI o owner finale non conformi.'
    }
}

# ====== dati sul canale seriale e buffer posseduto confrontati indipendentemente ===== #

function Test-UartTransmit($log, $registers, $owner) {
    $tx = Read-Hex $log 'KERNEL_UART_TX_OWNER: 0x([0-9a-f]{16})'
    $payload = [Text.Encoding]::ASCII.GetBytes("KERNEL_UART_TX_FINAL: OK`r`n")
    $first = $log.IndexOf('KERNEL_UART_TX_PAYLOAD: 0123456789abcdef')
    $second = $log.IndexOf('KERNEL_UART_TX_REUSE: 0123456789abcdef0123456789abcdef01234567')
    $third = $log.IndexOf('KERNEL_UART_TX_FINAL: OK')
    if (!$log.Contains('KERNEL_UART_TX_OK') -or ($tx -band 7) -or
        $tx -lt 0x100000000 -or $tx+88 -gt 0x100100000 -or
        (Read-Hex $log 'KERNEL_UART_TX_BYTES: 0x([0-9a-f]{16})') -ne $payload.Length -or
        (Read-Hex $log 'KERNEL_UART_TX_TOTAL: 0x([0-9a-f]{16})') -ne 134 -or
        (Read-Hex $log 'KERNEL_UART_TX_COMPLETED: 0x([0-9a-f]{16})') -ne 3 -or
        $first -lt 0 -or $second -le $first -or $third -le $second -or
        [regex]::Matches($log,'\r?\nKERNEL_UART_TX_PAYLOAD: 0123456789abcdef\r?\n').Count -ne 1 -or
        [regex]::Matches($log,'\r?\nKERNEL_UART_TX_REUSE: 0123456789abcdef0123456789abcdef01234567\r?\n').Count -ne 1 -or
        [regex]::Matches($log,'\r?\nKERNEL_UART_TX_FINAL: OK\r?\n').Count -ne 1 -or
        $registers -notmatch ('(?im)^'+($owner+32).ToString('x16')+':\s+0x'+$tx.ToString('x16')+'\s+0x0000000300000071')) {
        throw 'Payload TX, lunghezza o owner non conformi.'
    }
    $expected = [byte[]]::new(88)
    [BitConverter]::GetBytes([uint64]$tx).CopyTo($expected,0)
    [BitConverter]::GetBytes([uint32]$payload.Length).CopyTo($expected,8)
    [BitConverter]::GetBytes([uint32]$payload.Length).CopyTo($expected,12)
    [BitConverter]::GetBytes([uint32]1).CopyTo($expected,16)
    $payload.CopyTo($expected,24)
    for ($offset=0; $offset -lt 88; $offset+=16) {
        $pattern = '(?im)^'+($tx+$offset).ToString('x16')+':\s+0x'+[BitConverter]::ToUInt64($expected,$offset).ToString('x16')
        if ($offset+8 -lt 88) { $pattern += '\s+0x'+[BitConverter]::ToUInt64($expected,$offset+8).ToString('x16') }
        if ($registers -notmatch $pattern) { throw 'Dati o metadati TX diversi dalla copia posseduta.' }
    }
}

# ============ coda svuotata e slot azzerati dopo tre messaggi verificati ============ #

function Test-UartQueue($log, $registers) {
    $queue = Read-Hex $log 'KERNEL_UART_QUEUE_OWNER: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_UART_QUEUE_OK') -or ($queue -band 7) -or
        $queue -lt 0x100000000 -or $queue+272 -gt 0x100100000 -or
        (Read-Hex $log 'KERNEL_UART_QUEUE_FULL: 0x([0-9a-f]{16})') -ne 1) {
        throw 'Coda UART: owner o rifiuto della quarta submission non conforme.'
    }
    $expected = [byte[]]::new(272)
    [BitConverter]::GetBytes([uint64]$queue).CopyTo($expected,0)
    [BitConverter]::GetBytes([uint64]4).CopyTo($expected,8)
    [BitConverter]::GetBytes([uint64]3).CopyTo($expected,16)
    for ($offset=0; $offset -lt 272; $offset+=16) {
        $pattern = '(?im)^'+($queue+$offset).ToString('x16')+':\s+0x'+[BitConverter]::ToUInt64($expected,$offset).ToString('x16')+
            '\s+0x'+[BitConverter]::ToUInt64($expected,$offset+8).ToString('x16')
        if ($registers -notmatch $pattern) { throw 'Coda UART: completamento o slot residui non conformi.' }
    }
}

# ====== owner MMIO persistente distinto dalle guardie vuote ===== #

function Test-LapicRetention($log, $registers) {
    $mapping = Read-Hex $log 'KERNEL_LAPIC_MAPPING: 0x([0-9a-f]{16})'
    $virtual = Read-Hex $log 'KERNEL_LAPIC_VIRTUAL: 0x([0-9a-f]{16})'
    $entry = Read-Hex $log 'KERNEL_LAPIC_PTE: 0x([0-9a-f]{16})'
    $physical = Read-Hex $log 'KERNEL_MADT_LAPIC: 0x([0-9a-f]{16})'
    $cache = Read-Hex $log 'KERNEL_APIC_CACHE_BITS: 0x([0-9a-f]{16})'
    $expected = [Convert]::ToUInt64('8000000000000003',16) -bor $physical -bor $cache
    $ist = Read-Hex $log 'KERNEL_IST_PTE: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_LAPIC_MAPPING_OK') -or $entry -ne $expected -or
        $virtual -ne [uint64]123145303490560 -or ($mapping -band 7) -or
        $mapping -lt 0x100000000 -or $mapping+40 -gt 0x100100000) { throw 'Mapping LAPIC non conforme.' }
    $root = Read-Hex $registers 'CR3=([0-9a-f]{16})'
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $raw = [regex]::Match($registers,'(?im)^'+($profile+32).ToString('x16')+':\s+0x[0-9a-f]{16}\s+0x([0-9a-f]{16})')
    if (!$raw.Success) { throw 'CPUID MAXPHYADDR assente nello snapshot APIC.' }
    $bits = [Convert]::ToUInt64($raw.Groups[1].Value,16) -band 4294967295L
    if ($registers -notmatch ('(?im)^'+$mapping.ToString('x16')+':\s+0x'+$mapping.ToString('x16')+'\s+0x'+$root.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($mapping+16).ToString('x16')+':\s+0x'+$physical.ToString('x16')+'\s+0x'+$cache.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($mapping+32).ToString('x16')+':\s+0x'+(($bits -shl 32)+1).ToString('x16'))) { throw 'Owner LAPIC persistente diverso dallo snapshot.' }
    $slot = $ist+(287-64)*8
    $row = [regex]::Match($registers,'(?im)^'+$slot.ToString('x16')+':\s+0x0000000000000000\s+0x([0-9a-f]{16})')
    if (!$row.Success -or ([Convert]::ToUInt64($row.Groups[1].Value,16) -band [Convert]::ToUInt64('ffffffffffffff9f',16)) -ne $expected -or
        $registers -notmatch ('(?im)^'+($slot+16).ToString('x16')+':\s+0x0000000000000000')) { throw 'PTE LAPIC persistente o guardie non conformi.' }
    Test-MixedInventory $log $registers $mapping $physical $cache $virtual
}

# ====== totali e registro dispositivo confrontati con la memoria del kernel ===== #

function Test-MixedInventory($log, $registers, $mapping, $physical, $cache, $virtual) {
    $paging = Read-Hex $log 'KERNEL_PAGING_MIXED_BASE: 0x([0-9a-f]{16})'
    $ram = Read-Hex $log 'KERNEL_PAGING_MIXED_RAM: 0x([0-9a-f]{16})'
    $mmio = Read-Hex $log 'KERNEL_PAGING_MIXED_MMIO: 0x([0-9a-f]{16})'
    $total = Read-Hex $log 'KERNEL_PAGING_MIXED_LEAVES: 0x([0-9a-f]{16})'
    # The framebuffer mapping (MMIO aperture) is published after this log line and belongs to the
    # final snapshot dumped at the halt.
    $finalMmio = [uint64]1
    if ($log.Contains('KERNEL_FB_MAPPING_LIVE') -and (Read-Hex $log 'KERNEL_FB_MAPPING_INVENTORY: 0x([0-9a-f]{16})') -eq $paging) {
        $finalMmio += Read-Hex $log 'KERNEL_FB_PLAN_PAGES: 0x([0-9a-f]{16})'
    }
    # G3: the kernel heap (RAM leaves) is bound after this log line as well.
    $finalRam = [uint64]$ram
    if ($log.Contains('KERNEL_HEAP_READY') -and (Read-Hex $log 'KERNEL_HEAP_INVENTORY: 0x([0-9a-f]{16})') -eq $paging) {
        $finalRam += Read-Hex $log 'KERNEL_HEAP_PAGES: 0x([0-9a-f]{16})'
    }
    if (!$log.Contains('KERNEL_PAGING_MIXED_OK') -or $mmio -ne 1 -or $ram+1 -ne $total -or
        $registers -notmatch ('(?im)^'+($paging+3096).ToString('x16')+':\s+0x'+$finalRam.ToString('x16')+'\s+0x'+$finalMmio.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($paging+3112).ToString('x16')+':\s+0x'+$virtual.ToString('x16')+'\s+0x'+$physical.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($paging+3128).ToString('x16')+':\s+0x'+$cache.ToString('x16')+'\s+0x'+$mapping.ToString('x16'))) { throw 'Inventario RAM/MMIO diverso dallo snapshot.' }
}
