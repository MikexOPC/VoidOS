# ============ registro persistente con tutte le subscription rimosse ============ #

function Write-IrqRegistryMonitor($log, $vm) {
    $registry = Read-Hex $log 'KERNEL_IRQ_REGISTRY: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /15gx 0x'+$registry.ToString('x16'))
}

# ============ ogni qword corrotta e un contatore falso devono essere rifiutati ============ #

function Test-IrqRegistryMonitorRejections($log, $registers) {
    Test-IrqRegistryFirmware $log $registers
    $registry = Read-Hex $log 'KERNEL_IRQ_REGISTRY: 0x([0-9a-f]{16})'
    for ($offset=0; $offset -lt 120; $offset+=8) {
        $address = ($registry+($offset -band -16)).ToString('x16')
        $row = [regex]::Match($registers,'(?im)^'+$address+':\s+0x([0-9a-f]{16})(?:\s+0x([0-9a-f]{16}))?')
        if (!$row.Success) { throw 'Fixture registro IRQ incompleta.' }
        $left = [Convert]::ToUInt64($row.Groups[1].Value,16)
        $right = if ($row.Groups[2].Success) { [Convert]::ToUInt64($row.Groups[2].Value,16) } else { 0 }
        if ($offset -band 8) { $right = $right -bxor 1 } else { $left = $left -bxor 1 }
        $changed = $address+': 0x'+$left.ToString('x16')
        if ($row.Groups[2].Success) { $changed += ' 0x'+$right.ToString('x16') }
        $rejected = $false
        try { Test-IrqRegistryFirmware $log ($registers.Replace($row.Value,$changed)) } catch { $rejected = $true }
        if (!$rejected) { throw "Corruzione registro IRQ accettata a offset $offset." }
    }
    $changed = $log.Replace('KERNEL_IRQ_REGISTRY_DELIVERED: 0x000000000000008c',
        'KERNEL_IRQ_REGISTRY_DELIVERED: 0x000000000000008b')
    $rejected = $false
    try { Test-IrqRegistryFirmware $changed $registers } catch { $rejected = $true }
    if (!$rejected) { throw 'Contatore consegne falso accettato.' }
    Write-Output 'Registro IRQ: fotografia reale valida e 16 alterazioni rifiutate.'
}

# ============ conferme dispatcher e fotografia completa del registro ============ #

function Test-IrqRegistryFirmware($log, $registers) {
    $registry = Read-Hex $log 'KERNEL_IRQ_REGISTRY: 0x([0-9a-f]{16})'
    if (!$log.Contains('KERNEL_IRQ_REGISTRY_OK') -or ($registry -band 7) -or
        $registry -lt 0x100000000 -or $registry+120 -gt 0x100100000 -or
        (Read-Hex $log 'KERNEL_IRQ_REGISTRY_DELIVERED: 0x([0-9a-f]{16})') -ne 140) {
        throw 'Registro IRQ: owner o consegne confermate non conformi.'
    }
    $expected = [byte[]]::new(120)
    [BitConverter]::GetBytes([uint64]$registry).CopyTo($expected,0)
    [BitConverter]::GetBytes([uint64]0x100000000).CopyTo($expected,8)
    [BitConverter]::GetBytes([uint64]0x100100000).CopyTo($expected,16)
    [BitConverter]::GetBytes([uint64]3).CopyTo($expected,24)
    for ($offset=0; $offset -lt 120; $offset+=16) {
        $pattern = '(?im)^'+($registry+$offset).ToString('x16')+':\s+0x'+[BitConverter]::ToUInt64($expected,$offset).ToString('x16')
        if ($offset+8 -lt 120) { $pattern += '\s+0x'+[BitConverter]::ToUInt64($expected,$offset+8).ToString('x16') }
        if ($registers -notmatch $pattern) { throw 'Registro IRQ: metadati o subscription residue non conformi.' }
    }
}
