. (Join-Path $PSScriptRoot '../../../../../../../scripts/lib/layout.ps1')
# ============ mandatory fields and closed owner ============

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text,$Pattern)
    if (!$match.Success) { throw "Missing field: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}

# ============ final state after any preliminary failure report ============

function Read-LastHex([string]$Text, [string]$Pattern) {
    $matches=[regex]::Matches($Text,$Pattern)
    if (!$matches.Count) {throw "Missing final field: $Pattern"}
    return [Convert]::ToUInt64($matches[$matches.Count-1].Groups[1].Value,16)
}

function Test-WatchdogOwner([string]$Log, [string]$Monitor, [uint64]$Expected, [uint64]$Result,
    [uint64]$Timer=0, [uint64]$Pin=16, [uint64]$Cleanup=0, [uint64]$Attempts=0) {
    $owner = Read-Hex $Log 'KERNEL_WATCHDOG_OWNER: 0x([0-9a-f]{16})'
    $failure = if ($Result -eq 2) {16} else {0}
    $phase = if ($Result -eq 2) {'DEADLINE'} else {'NONE'}
    $cleanupPhase = if ($Cleanup -eq 20) {'RESTORE_DEVICE'} else {'NONE'}
    if ((Read-Hex $Log 'KERNEL_WATCHDOG_FAILURE: 0x([0-9a-f]{16})') -ne $failure -or
        (Read-Hex $Log 'KERNEL_WATCHDOG_CLEANUP_FAILURE: 0x([0-9a-f]{16})') -ne $Cleanup -or
        $Log -notmatch ('(?m)^KERNEL_WATCHDOG_FAILURE_PHASE: '+$phase+'\r?$') -or
        $Log -notmatch ('(?m)^KERNEL_WATCHDOG_CLEANUP_PHASE: '+$cleanupPhase+'\r?$') -or
        (Read-LastHex $Log 'KERNEL_WATCHDOG_RECOVERY_STATE: 0x([0-9a-f]{16})') -ne 6 -or
        (Read-LastHex $Log 'KERNEL_WATCHDOG_RECOVERY_ATTEMPTS: 0x([0-9a-f]{16})') -ne $Attempts) {
        throw 'Unexpected operational or cleanup failure phase.'
    }
    if (($owner -band 7) -or $owner -lt $OsImageBase -or $owner+224 -gt $OsImageLimit -or
        !$Log.Contains('KERNEL_WATCHDOG_CLEANUP_OK') -or
        (Read-Hex $Log 'KERNEL_WATCHDOG_FIRED: 0x([0-9a-f]{16})') -ne $Expected -or
        (Read-Hex $Log 'KERNEL_WATCHDOG_RESULT: 0x([0-9a-f]{16})') -ne $Result -or
        (Read-TimerWord $Monitor $owner) -ne $Expected -or
        (Read-TimerWord $Monitor ($owner+8)) -ne $owner -or
        (Read-TimerWord $Monitor ($owner+80)) -ne 0 -or
        (Read-TimerWord $Monitor ($owner+88)) -ne 0 -or
        (Read-TimerWord $Monitor ($owner+96)) -ne 0xfed00000L -or
        (Read-TimerWord $Monitor ($owner+200)) -ne 0 -or
        (Read-TimerWord $Monitor ($owner+208)) -ne 0 -or
        (Read-TimerWord $Monitor ($owner+216)) -ne ((1L -shl 32)+$Result)) {
        throw 'Watchdog owner, handler or resources not closed.'
    }
    if ((Read-Hex $Log 'KERNEL_WATCHDOG_TIMER: 0x([0-9a-f]{16})') -ne $Timer -or
        (Read-Hex $Log 'KERNEL_WATCHDOG_PIN: 0x([0-9a-f]{16})') -ne $Pin -or
        (Read-Hex $Log 'KERNEL_WATCHDOG_TICKS: 0x([0-9a-f]{16})') -ne 10000000) {
        throw 'Unexpected q35 HPET route or deadline.'
    }
}
