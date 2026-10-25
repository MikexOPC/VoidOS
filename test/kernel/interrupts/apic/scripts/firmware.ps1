. (Join-Path $PSScriptRoot '../../../../../scripts/lib/layout.ps1')
# ====== copia del profilo CPU e stato LAPIC dal monitor indipendente ===== #

function Write-ApicMonitor($log, $vm) {
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /10gx 0x' + $profile.ToString('x16'))
    $vm.StandardInput.WriteLine('info lapic')
    $capabilities = Read-Hex $log 'KERNEL_APIC_CAPABILITIES: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /66gx 0x' + $capabilities.ToString('x16'))
    $vm.StandardInput.WriteLine('info pic')
    $idt = Read-Hex $log 'KERNEL_IRQ_IDT_BASE: 0x([0-9a-f]{16})'
    foreach ($vector in @(0x40,0xf0,0xff,2,8)) {
        $vm.StandardInput.WriteLine('x /2gx 0x' + ($idt+$vector*16).ToString('x16'))
    }
}

# ====== verifica PAT corrente PTE previste e identita iniziale del BSP ===== #

function Test-ApicFirmware($log, $registers) {
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $base = Read-Hex $log 'KERNEL_APIC_BASE_MSR: 0x([0-9a-f]{16})'
    $pat = Read-Hex $log 'KERNEL_APIC_PAT: 0x([0-9a-f]{16})'
    $id = Read-Hex $log 'KERNEL_APIC_INITIAL_ID: 0x([0-9a-f]{16})'
    $uid = Read-Hex $log 'KERNEL_APIC_BSP_UID: 0x([0-9a-f]{16})'
    $slot = Read-Hex $log 'KERNEL_APIC_UC_INDEX: 0x([0-9a-f]{16})'
    $flags = Read-Hex $log 'KERNEL_APIC_CACHE_BITS: 0x([0-9a-f]{16})'
    $lapic = Read-Hex $log 'KERNEL_MADT_LAPIC: 0x([0-9a-f]{16})'
    $flagsRow = [regex]::Match($registers, '(?im)^' + ($profile + 16).ToString('x16') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
    if (!$flagsRow.Success) { throw 'Snapshot RFLAGS/CPUID assente.' }
    $rflags = [Convert]::ToUInt64($flagsRow.Groups[1].Value,16)
    $leaf1 = [Convert]::ToUInt64($flagsRow.Groups[2].Value,16)
    if (!$log.Contains('KERNEL_APIC_PREFLIGHT_OK') -or ($profile -band 7) -or
        $profile -lt $OsImageBase -or $profile + 80 -gt $OsImageLimit -or
        ($base -band 0xd00) -ne 0x900 -or ($base -band [uint64]68719472640) -ne $lapic -or
        ($pat -band 255) -ne 6 -or $slot -lt 1 -or $slot -gt 7 -or
        (($pat -shr ([int]$slot * 8)) -band 255) -ne 0 -or
        $flags -ne (($slot -band 1) * 8 + ($slot -band 2) * 8 + ($slot -band 4) * 32) -or
        ($rflags -band 0x202) -ne 2 -or (($leaf1 -shr 24) -band 255) -ne $id -or
        $registers -notmatch ('(?im)^' + $profile.ToString('x16') + ':\s+0x' + $base.ToString('x16') + '\s+0x' + $pat.ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($profile + 48).ToString('x16') + ':\s+0x' + $lapic.ToString('x16') + '\s+0x' + $flags.ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($profile + 64).ToString('x16') + ':\s+0x' + (($id -shl 32) + $uid).ToString('x16') + '\s+0x' + ((1L -shl 32) + $slot).ToString('x16'))) {
        throw 'Preflight APIC diverso da snapshot CPU, MADT o selezione PAT UC.'
    }
    Test-ApicCapabilities $log $registers
    Test-IrqFirmware $log $registers
}

# ====== fotografia hardware e code zero dopo la revoca dei mapping MMIO ===== #

function Test-ApicCapabilities($log, $registers) {
    $target = Read-Hex $log 'KERNEL_APIC_CAPABILITIES: 0x([0-9a-f]{16})'
    $id = Read-Hex $log 'KERNEL_LAPIC_CURRENT_ID: 0x([0-9a-f]{16})'
    $version = Read-Hex $log 'KERNEL_LAPIC_VERSION: 0x([0-9a-f]{16})'
    $physical = Read-Hex $log 'KERNEL_IO_APIC_ADDRESS: 0x([0-9a-f]{16})'
    $ioId = Read-Hex $log 'KERNEL_IO_APIC_ID: 0x([0-9a-f]{16})'
    $base = Read-Hex $log 'KERNEL_IO_APIC_BASE: 0x([0-9a-f]{16})'
    $end = Read-Hex $log 'KERNEL_IO_APIC_END: 0x([0-9a-f]{16})'
    $inputs = Read-Hex $log 'KERNEL_IO_APIC_INPUTS: 0x([0-9a-f]{16})'
    $ioVersion = Read-Hex $log 'KERNEL_IO_APIC_VERSION: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_APIC_MMIO_OK') -or ($target -band 7) -or $target -lt $OsImageBase -or
        $target + 528 -gt $OsImageLimit -or $id -ne (Read-Hex $log 'KERNEL_APIC_INITIAL_ID: 0x([0-9a-f]{16})') -or
        (Read-Hex $log 'KERNEL_IO_APIC_COUNT: 0x([0-9a-f]{16})') -ne 1 -or
        $physical -ne [uint64]4273995776 -or $ioVersion -ne 0x00170020 -or $inputs -ne 24 -or $base -ne 0 -or $end -ne 24 -or
        $registers -notmatch ('(?im)^'+$target.ToString('x16')+':\s+0x'+(($version -shl 32)+$id).ToString('x16')+'\s+0x0000000100000001') -or
        $registers -notmatch ('(?im)^'+($target+16).ToString('x16')+':\s+0x'+$physical.ToString('x16')+'\s+0x'+$end.ToString('x16')) -or
        $registers -notmatch ('(?im)^'+($target+32).ToString('x16')+':\s+0x'+(($base -shl 32)+$ioId).ToString('x16')+'\s+0x'+(($inputs -shl 32)+$ioVersion).ToString('x16'))) {
        throw 'Capacita APIC diverse dai registri del modello QEMU o dalla copia persistente.'
    }
    for ($row = 0; $row -lt 30; ++$row) {
        if ($registers -notmatch ('(?im)^'+($target+48+$row*16).ToString('x16')+':\s+0x0000000000000000\s+0x0000000000000000')) {
            throw 'Coda delle capacita IOAPIC non azzerata.'
        }
    }
}

# ====== ritorno verificato e stato mascherato letto dal monitor indipendente ===== #

function Test-IrqFirmware($log, $registers) {
    if (!$log.Contains('KERNEL_IRQ_RETURN_OK') -or !$log.Contains('KERNEL_APIC_MASKED_OK') -or
        (Read-Hex $log 'KERNEL_IRQ_RETURN_COUNT: 0x([0-9a-f]{16})') -ne 1 -or
        (Read-Hex $log 'KERNEL_IRQ_SPURIOUS_COUNT: 0x([0-9a-f]{16})') -ne 1 -or
        (Read-Hex $log 'KERNEL_MASKED_IO_INPUTS: 0x([0-9a-f]{16})') -ne 24 -or
        (Read-Hex $log 'KERNEL_MASKED_LVT_COUNT: 0x([0-9a-f]{16})') -ne 6 -or
        $registers -notmatch '(?im)^SPIV\s+0x000001ff' -or
        $registers -notmatch '(?im)^ISR\s+\(none\)' -or $registers -notmatch '(?im)^IRR\s+\(none\)') {
        throw 'Ritorno IRQ o controller mascherati diversi dal profilo verificato.'
    }
    if ($registers -notmatch '(?im)^LVT1\s+0x00000400') { throw 'NMI locale MADT non configurato su LINT1.' }
    foreach ($name in @('LVT0','LVTPC','LVTERR','LVTTHMR','LVTT')) {
        if ($registers -notmatch ('(?im)^'+$name+'\s+0x00010040')) { throw "LVT non mascherato: $name" }
    }
    foreach ($pic in @(0,1)) {
        if ($registers -notmatch ('(?im)^pic'+$pic+':.*\bimr=ff\b.*\bisr=00\b')) { throw 'PIC legacy aperto o in servizio.' }
    }
    for ($pin = 0; $pin -lt 24; ++$pin) {
        if ($registers -notmatch ('(?im)^\s+pin '+$pin+'\s+0x0000000000010040\s+dest=0\s+vec=64\s+active-hi edge\s+masked fixed\s+physical')) {
            throw "Rotta I/O APIC non mascherata: $pin"
        }
    }
    $idt = Read-Hex $log 'KERNEL_IRQ_IDT_BASE: 0x([0-9a-f]{16})'
    foreach ($vector in @(0x40,0xf0,0xff,2,8)) {
        $row = [regex]::Match($registers,'(?im)^'+($idt+$vector*16).ToString('x16')+':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
        if (!$row.Success) { throw 'Gate IRQ assente dal monitor.' }
        $low = [Convert]::ToUInt64($row.Groups[1].Value,16)
        $high = [Convert]::ToUInt64($row.Groups[2].Value,16)
        $ist = if ($vector -eq 2) {2} elseif ($vector -eq 8) {3} else {0}
        $address = ($low -band 65535) + (($low -shr 48) -shl 16) + (($high -band 4294967295L) -shl 32)
        if (($low -shr 16 -band 65535) -ne 8 -or ($low -shr 32 -band 255) -ne $ist -or
            ($low -shr 40 -band 255) -ne 0x8e -or ($high -shr 32) -or
            $address -lt $OsImageBase -or $address -ge $OsImageLimit) { throw 'Gate IRQ corrotto o IST errato.' }
    }
}
