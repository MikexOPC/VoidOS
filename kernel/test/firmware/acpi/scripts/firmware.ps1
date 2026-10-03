# ====== contratto ACPI persistente dopo revoca dello stack loader ===== #

function Test-AcpiFirmware($log, $registers, $arenaContext, $pagingInventory, $case, $windowPte) {
    $acpiDirectory = Read-Hex $log 'KERNEL_ACPI_DIRECTORY: 0x([0-9a-f]{16})'
    $acpiPhysical = Read-Hex $log 'KERNEL_ACPI_RSDP: 0x([0-9a-f]{16})'
    $acpiRoot = Read-Hex $log 'KERNEL_ACPI_ROOT: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_ACPI_RSDP_OK') -or
        $acpiPhysical -ne (Read-Hex $log 'ACPI_RSDP_PHYSICAL: 0x([0-9A-F]{16})') -or
        $acpiRoot -ne (Read-Hex $log 'ACPI_ROOT_PHYSICAL: 0x([0-9A-F]{16})') -or
        $registers -notmatch ('(?im)^' + ($arenaContext + 112).ToString('x16') + ':\s+0x' + $acpiPhysical.ToString('x16') + '\s+0x0000000200000024') -or
        $registers -notmatch ('(?im)^' + ($arenaContext + 128).ToString('x16') + ':\s+0x2052545020445352') -or
        $registers -notmatch ('(?im)^' + ($arenaContext + 144).ToString('x16') + ':\s+0x[0-9a-f]{16}\s+0x' + $acpiRoot.ToString('x16'))) {
        throw 'Snapshot ACPI persistente diverso dalla discovery firmware.'
    }
    $acpiCount = Read-Hex $log 'KERNEL_ACPI_TABLE_COUNT: 0x([0-9a-f]{16})'
    $acpiBytes = Read-Hex $log 'KERNEL_ACPI_TABLE_BYTES: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_ACPI_XSDT_OK') -or !$log.Contains('KERNEL_ACPI_DIRECTORY_OK') -or
        !$acpiCount -or $acpiCount -gt 256 -or $acpiBytes -ne 36 + 8 * $acpiCount -or
        ($acpiDirectory -band 7) -or $acpiDirectory -lt $pagingInventory + 3096 -or
        $acpiDirectory + 2072 -gt 0x7000000b3000 -or
        $registers -notmatch ('(?im)^' + $acpiDirectory.ToString('x16') + ':\s+0x' + $acpiRoot.ToString('x16') + '\s+0x' + (($acpiCount -shl 32) + $acpiBytes).ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($acpiDirectory + 16).ToString('x16') + ':\s+0x0000000000000008')) {
        throw 'Directory XSDT fuori arena o diversa dai dati verificati dal kernel.'
    }
    if ($case.Variant -notin @('execute','guard-low','guard-high')) {
        for ($slot = 0; $slot -lt 16; $slot += 2) {
            if ($registers -notmatch ('(?im)^' + ($windowPte + $slot * 8).ToString('x16') + ':\s+0x0000000000000000\s+0x0000000000000000')) {
                throw 'Mapping temporaneo ACPI ancora presente nella finestra.'
            }
        }
    }
    Test-MadtFirmware $log $registers $acpiDirectory $case
}

# ====== monitor della sola RAM contenente MADT e copia persistente ===== #

function Write-MadtMonitor($log, $vm) {
    $topology = Read-Hex $log 'KERNEL_MADT_TOPOLOGY: 0x([0-9a-f]{16})'
    $physical = Read-Hex $log 'KERNEL_MADT_PHYSICAL: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /8gx 0x' + $topology.ToString('x16'))
    $vm.StandardInput.WriteLine('x /2gx 0x' + ($topology + 64).ToString('x16'))
    $vm.StandardInput.WriteLine('xp /6gx 0x' + $physical.ToString('x16'))
}

# ====== confronto con header firmware senza assumere UID uguale all indice CPU ===== #

function Test-MadtFirmware($log, $registers, $directory, $case) {
    $topology = Read-Hex $log 'KERNEL_MADT_TOPOLOGY: 0x([0-9a-f]{16})'
    $physical = Read-Hex $log 'KERNEL_MADT_PHYSICAL: 0x([0-9a-f]{16})'
    $lapic = Read-Hex $log 'KERNEL_MADT_LAPIC: 0x([0-9a-f]{16})'
    $cpus = Read-Hex $log 'KERNEL_MADT_CPU_COUNT: 0x([0-9a-f]{16})'
    $io = Read-Hex $log 'KERNEL_MADT_IO_COUNT: 0x([0-9a-f]{16})'
    $iso = Read-Hex $log 'KERNEL_MADT_ISO_COUNT: 0x([0-9a-f]{16})'
    $nmi = Read-Hex $log 'KERNEL_MADT_NMI_COUNT: 0x([0-9a-f]{16})'
    $expected = if ($case.Smp) {[uint64]$case.Smp} else {[uint64]1}
    if (!$log.Contains('KERNEL_MADT_DISCOVERY_OK') -or $cpus -ne $expected -or
        (Read-Hex $log 'KERNEL_MADT_ENABLED_COUNT: 0x([0-9a-f]{16})') -ne $expected -or
        (Read-Hex $log 'KERNEL_MADT_UNKNOWN_COUNT: 0x([0-9a-f]{16})') -ne 0 -or
        !$io -or $io -gt 16 -or $iso -gt 16 -or !$nmi -or $nmi -gt 64 -or
        ($topology -band 7) -or $topology -lt $directory + 2072 -or $topology + 2624 -gt 0x7000000b3000) {
        throw 'Topologia MADT assente, fuori arena o conteggi CPU errati.'
    }
    $first = [regex]::Match($registers, '(?im)^0*' + $physical.ToString('x').TrimStart('0') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
    $last = [regex]::Match($registers, '(?im)^0*' + ($physical + 32).ToString('x').TrimStart('0') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
    if (!$first.Success -or !$last.Success) { throw 'Header MADT originale assente dal monitor fisico.' }
    $word = [Convert]::ToUInt64($first.Groups[1].Value,16)
    $revision = [Convert]::ToUInt64($first.Groups[2].Value,16) -band 255
    $length = $word -shr 32
    $originalLapic = [Convert]::ToUInt64($last.Groups[1].Value,16) -shr 32
    $pcat = [Convert]::ToUInt64($last.Groups[2].Value,16) -band 1
    if (($word -band [uint64]4294967295) -ne 0x43495041 -or $revision -lt 1 -or $revision -gt 6 -or
        $length -lt 44 -or $length -gt 65536 -or $lapic -ne $originalLapic -or
        $registers -notmatch ('(?im)^' + $topology.ToString('x16') + ':\s+0x' + $physical.ToString('x16') + '\s+0x' + $lapic.ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($topology + 16).ToString('x16') + ':\s+0x' + (($revision -shl 32) + $length).ToString('x16') + '\s+0x' + $pcat.ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($topology + 32).ToString('x16') + ':\s+0x' + (($io -shl 32) + $cpus).ToString('x16') + '\s+0x' + (($nmi -shl 32) + $iso).ToString('x16')) -or
        $registers -notmatch ('(?im)^' + ($topology + 48).ToString('x16') + ':\s+0x0000000100000000\s+0x0000000000000000') -or
        $registers -notmatch ('(?im)^' + ($topology + 64).ToString('x16') + ':\s+0x[0-9a-f]{16}\s+0x0000000000000001')) {
        throw 'Fotografia MADT diversa da header, conteggi o flags verificati.'
    }
}
