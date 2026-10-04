# ====== snapshot owner UART dopo la chiusura della sorgente ===== #

function Write-UartMonitor($log, $vm) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /4gx 0x' + $owner.ToString('x16'))
    $mapping = Read-Hex $log 'KERNEL_LAPIC_MAPPING: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /5gx 0x' + $mapping.ToString('x16'))
    $ist = Read-Hex $log 'KERNEL_IST_PTE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /3gx 0x' + ($ist+(287-64)*8).ToString('x16'))
    $paging = Read-Hex $log 'KERNEL_PAGING_MIXED_BASE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /6gx 0x' + ($paging+3096).ToString('x16'))
}

# ====== tre IRQ hardware distinti e owner persistente non piu attivo ===== #

function Test-UartFirmware($log, $registers, $cpuLog) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $row = [regex]::Match($registers,'(?im)^'+($owner+16).ToString('x16')+':\s+0x'+$profile.ToString('x16')+'\s+0x0000000000000000')
    $trace = Get-Content -LiteralPath $cpuLog -Raw
    Test-LapicRetention $log $registers
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
    if (!$log.Contains('KERNEL_PAGING_MIXED_OK') -or $mmio -ne 1 -or $ram+1 -ne $total -or
        $registers -notmatch ('(?im)^'+($paging+3096).ToString('x16')+':\s+0x'+$ram.ToString('x16')+'\s+0x0000000000000001') -or
        $registers -notmatch ('(?im)^'+($paging+3112).ToString('x16')+':\s+0x'+$virtual.ToString('x16')+'\s+0x'+$physical.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($paging+3128).ToString('x16')+':\s+0x'+$cache.ToString('x16')+'\s+0x'+$mapping.ToString('x16'))) { throw 'Inventario RAM/MMIO diverso dallo snapshot.' }
}
