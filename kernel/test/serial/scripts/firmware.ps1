# ====== snapshot owner UART dopo la chiusura della sorgente ===== #

function Write-UartMonitor($log, $vm) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $vm.StandardInput.WriteLine('x /4gx 0x' + $owner.ToString('x16'))
}

# ====== tre IRQ hardware distinti e owner persistente non piu attivo ===== #

function Test-UartFirmware($log, $registers, $cpuLog) {
    $owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
    $profile = Read-Hex $log 'KERNEL_APIC_PROFILE: 0x([0-9a-f]{16})'
    $row = [regex]::Match($registers,'(?im)^'+($owner+16).ToString('x16')+':\s+0x'+$profile.ToString('x16')+'\s+0x0000000000000000')
    $trace = Get-Content -LiteralPath $cpuLog -Raw
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
