# ====== copia del profilo CPU e stato LAPIC dal monitor indipendente ===== #

function Write-ApicMonitor($log, $vm) {
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /10gx 0x' + $profile.ToString('x16'))
    $vm.StandardInput.WriteLine('info lapic')
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
}
