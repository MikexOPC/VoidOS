param([Parameter(Mandatory=$true)][string]$ScenarioDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../scripts/firmware.ps1')

# ============ lettura indipendente della fotografia QEMU ============ #

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text,$Pattern)
    if (!$match.Success) { throw "Campo assente: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}
$log = Get-Content -LiteralPath (Join-Path $ScenarioDirectory 'serial.log') -Raw
$registers = Get-Content -LiteralPath (Join-Path $ScenarioDirectory 'registers.log') -Raw
Test-UartQueue $log $registers
$queue = Read-Hex $log 'KERNEL_UART_QUEUE_OWNER: 0x([0-9a-f]{16})'

# ============ ogni qword corrotta deve far fallire la verifica ============ #

for ($offset=0; $offset -lt 272; $offset+=8) {
    $address = ($queue+($offset -band -16)).ToString('x16')
    $row = [regex]::Match($registers,'(?im)^'+$address+':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
    if (!$row.Success) { throw 'Riga queue assente dalla fixture QEMU.' }
    $left = [Convert]::ToUInt64($row.Groups[1].Value,16)
    $right = [Convert]::ToUInt64($row.Groups[2].Value,16)
    if ($offset -band 8) { $right = $right -bxor 1 } else { $left = $left -bxor 1 }
    $changed = $address+': 0x'+$left.ToString('x16')+' 0x'+$right.ToString('x16')
    $rejected = $false
    try { Test-UartQueue $log ($registers.Replace($row.Value,$changed)) } catch { $rejected = $true }
    if (!$rejected) { throw "Corruzione queue accettata a offset $offset." }
}

# ============ snapshot incompleto e coda piena non provata sono rifiutati ============ #

$row = [regex]::Match($registers,'(?im)^'+$queue.ToString('x16')+':[^\r\n]*')
$rejected = $false
try { Test-UartQueue $log ($registers.Replace($row.Value,'')) } catch { $rejected = $true }
if (!$rejected) { throw 'Snapshot queue incompleto accettato.' }
$rejected = $false
try { Test-UartQueue ($log.Replace('KERNEL_UART_QUEUE_FULL: 0x0000000000000001',
    'KERNEL_UART_QUEUE_FULL: 0x0000000000000000')) $registers } catch { $rejected = $true }
if (!$rejected) { throw 'Rifiuto queue piena non verificato accettato.' }
# ============ ordine FIFO verificato anche contro payload scambiati ============ #

$owner = Read-Hex $log 'KERNEL_UART_IRQ_OWNER: 0x([0-9a-f]{16})'
Test-UartTransmit $log $registers $owner
$first = 'KERNEL_UART_TX_PAYLOAD: 0123456789abcdef'
$second = 'KERNEL_UART_TX_REUSE: 0123456789abcdef0123456789abcdef01234567'
$swapped = $log.Replace($first,'QUEUE_SWAP_FIXTURE').Replace($second,$first).Replace('QUEUE_SWAP_FIXTURE',$second)
$rejected = $false
try { Test-UartTransmit $swapped $registers $owner } catch { $rejected = $true }
if (!$rejected) { throw 'Payload scambiati accettati come FIFO.' }
Write-Output 'Coda TX: fotografia reale valida e 37 alterazioni rifiutate.'
