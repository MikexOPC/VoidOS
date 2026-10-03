# ====== copia del profilo CPU e stato LAPIC dal monitor indipendente ===== #

function Write-ApicMonitor($log, $vm) {
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /10gx 0x' + $profile.ToString('x16'))
    $vm.StandardInput.WriteLine('info lapic')
    $capabilities = Read-Hex $log 'KERNEL_APIC_CAPABILITIES: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /66gx 0x' + $capabilities.ToString('x16'))
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
        $profile -lt 0x100000000 -or $profile + 80 -gt 0x100100000 -or
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
    if (!$log.Contains('KERNEL_APIC_MMIO_OK') -or ($target -band 7) -or $target -lt 0x100000000 -or
        $target + 528 -gt 0x100100000 -or $id -ne (Read-Hex $log 'KERNEL_APIC_INITIAL_ID: 0x([0-9a-f]{16})') -or
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
