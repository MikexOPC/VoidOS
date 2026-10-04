# ====== snapshot owner UART dopo la chiusura della sorgente ===== #

function Write-UartMonitor($log, $vm) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /4gx 0x' + $owner.ToString('x16'))
    $mapping = Read-Hex $log 'KERNEL_LAPIC_MAPPING: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /5gx 0x' + $mapping.ToString('x16'))
    $ist = Read-Hex $log 'KERNEL_IST_PTE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /3gx 0x' + ($ist+(287-64)*8).ToString('x16'))
}

# ====== tre IRQ hardware distinti e owner persistente non piu attivo ===== #

function Test-UartFirmware($log, $registers, $cpuLog) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $row = [regex]::Match($registers,'(?im)^'+($owner+16).ToString('x16')+':\s+0x'+$profile.ToString('x16')+'\s+0x0000000000000000')
    $trace = Get-Content -LiteralPath $cpuLog -Raw
    Test-LapicRevocation $log $registers
    if (!$log.Contains('KERNEL_UART_IRQ_OK') -or ($owner -band 7) -or
        $owner -lt 0x100000000 -or $owner+32 -gt 0x100100000 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_COUNT: 0x([0-9a-f]{16})') -ne 3 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_ERROR: 0x([0-9a-f]{16})') -ne 0 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_VECTOR: 0x([0-9a-f]{16})') -ne 64 -or
        (Read-Hex $log 'KERNEL_UART_IRQ_GSI: 0x([0-9a-f]{16})') -ne 4 -or
        !$row.Success -or $registers -notmatch ('(?im)^'+$owner.ToString('x16')+':\s+0x0000000000000003\s+0x[0-9a-f]{16}') -or
        [regex]::Matches($trace,'Servicing hardware INT=0x40').Count -ne 3) {
        throw 'COM1: consegne hardware, contatore, EOI o owner finale non conformi.'
    }
}

# ====== mapping distinto e revoca finale indipendente dei tre slot ===== #

function Test-LapicRevocation($log, $registers) {
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
    foreach ($offset in @(0,16,32)) {
        $zeros = if ($offset -eq 32) { '0x0000000000000000' } else { '0x0000000000000000\s+0x0000000000000000' }
        if ($registers -notmatch ('(?im)^'+($mapping+$offset).ToString('x16')+':\s+'+$zeros)) { throw 'Owner LAPIC non revocato.' }
    }
    $slot = $ist+(287-64)*8
    if ($registers -notmatch ('(?im)^'+$slot.ToString('x16')+':\s+0x0000000000000000\s+0x0000000000000000') -or
        $registers -notmatch ('(?im)^'+($slot+16).ToString('x16')+':\s+0x0000000000000000')) { throw 'PTE LAPIC o guardie residue.' }
}
