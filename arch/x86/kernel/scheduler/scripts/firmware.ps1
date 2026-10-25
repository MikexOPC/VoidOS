param([string]$KernelDirectory = 'D:/OS/kernel/build/scheduler-native',
      [string]$QemuDirectory = 'C:/Program Files/qemu', [int]$Boots = 10,
      [ValidateSet('none','guard-low','guard-high','nx','retired','rollback','blocked','irq-timeout','idle-timeout','idle-repeat-timeout')][string]$Fault = 'none')
. (Join-Path $PSScriptRoot '../../../../../scripts/lib/layout.ps1')
$ErrorActionPreference = 'Stop'
if ($Boots -lt 1 -or $Boots -gt 20) { throw 'Boot count 1..20' }
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$out = Join-Path $project ('kernel/build/scheduler-firmware-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$image = Join-Path $KernelDirectory 'kernel.elf'
$loader = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
$kernelHash = (Get-FileHash $image).Hash
$blocked = $Fault -eq 'blocked'
$timeout = $Fault -in @('irq-timeout','idle-timeout','idle-repeat-timeout')
$timeoutMarker = if ($Fault -eq 'idle-timeout') {'KERNEL_SCHEDULER_IDLE_TIMEOUT_OK'} elseif ($Fault -eq 'idle-repeat-timeout') {'KERNEL_SCHEDULER_IDLE_REPEAT_TIMEOUT_OK'} else {'KERNEL_SCHEDULER_IRQ_TIMEOUT_OK'}
$negative = $Fault -in @('guard-low','guard-high','nx','retired','blocked','irq-timeout','idle-timeout','idle-repeat-timeout')
$pteMask = [Convert]::ToUInt64('FFFFFFFFFFFFFF9F',16)
$pteFlags = [Convert]::ToUInt64('8000000000000003',16)
Get-FileHash $image,$loader | ConvertTo-Json | Out-File "$out/images.json" -Encoding utf8

# ============ numeric evidence ============

function Read-Hex([string]$Log, [string]$Name) {
    $match = [regex]::Match($Log, $Name + ': 0x([0-9a-fA-F]{16})')
    if (!$match.Success) { throw "Missing field: $Name" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}

# ============ consecutive owned QEMU boots ============

foreach ($bootIndex in 1..$Boots) {
    $scenario = Join-Path $out "boot-$bootIndex"
    $boot = Join-Path $scenario 'esp/EFI/BOOT'
    New-Item -ItemType Directory -Path $boot | Out-Null
    Copy-Item $loader "$boot/BOOTX64.EFI"
    Copy-Item $image "$boot/kernel.elf"
    if ((Get-FileHash "$boot/kernel.elf").Hash -ne $kernelHash) { throw 'ESP image mismatch' }
    [IO.File]::WriteAllText("$boot/boot-check.txt", "OS boot check`r`n", [Text.Encoding]::ASCII)
    $vars = Join-Path $scenario 'vars.fd'
    Copy-Item "$QemuDirectory/share/edk2-i386-vars.fd" $vars
    $serial = Join-Path $scenario 'serial.log'
    $cpuLog = Join-Path $scenario 'qemu.log'
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m','128','-smp','1','-net','none',
        '-d','int,cpu_reset','-D',$cpuLog.Replace('\','/'),'-display','none','-monitor','stdio',
        '-no-reboot','-serial',('file:' + $serial.Replace('\','/')),
        '-drive',('if=pflash,format=raw,readonly=on,file=' + "$QemuDirectory/share/edk2-x86_64-code.fd"),
        '-drive',('if=pflash,format=raw,file=' + $vars.Replace('\','/')),
        '-drive',('format=raw,file=fat:rw:' + (Join-Path $scenario 'esp').Replace('\','/')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = "$QemuDirectory/qemu-system-x86_64.exe"
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new(); $vm.StartInfo = $start
    try {
        if (!$vm.Start()) { throw 'QEMU failed to start' }
        $stdout = $vm.StandardOutput.ReadToEndAsync(); $stderr = $vm.StandardError.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds(40); $found = $false; $log = ''
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path $serial) {
                $stream = [IO.File]::Open($serial,'Open','Read','ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $raw = $reader.ReadToEnd() } finally { $reader.Dispose() }
                $log = [regex]::Replace($raw,'\x1b\[[0-?]*[ -/]*[@-~]','')
                if ((!$negative -and $log.Contains('KERNEL_EXCEPTION_HALTED')) -or
                    $log -match 'SCHEDULER_FAILED|SCHEDULER_.*INVALID|SCHEDULER_.*FAILED|EXPECTED_FAULT_MISSING') {
                    throw "Kernel fault: $scenario"
                }
                if ($log.Contains($(if ($timeout) {$timeoutMarker} elseif ($blocked) {'KERNEL_SCHEDULER_BLOCKED_OK'} elseif ($negative) {'KERNEL_EXCEPTION_HALTED'} else {'KERNEL_READY'}))) {
                    $found = $true; break
                }
            }
            if ($vm.HasExited) { throw 'VM exited before readiness' }
            Start-Sleep -Milliseconds 100
        }
        if (!$found) { throw "Kernel deadline expired: $scenario" }
        $markers = if ($timeout) { @('KERNEL_SCHEDULER_STACKS_OK',$timeoutMarker) }
        elseif ($blocked) { @('KERNEL_SCHEDULER_STACKS_OK','KERNEL_SCHEDULER_BLOCKED_OK') }
        elseif ($negative) { @('KERNEL_SCHEDULER_STACKS_OK','KERNEL_SCHEDULER_FAULT_READY') } else {
            @('KERNEL_SCHEDULER_STACKS_OK','KERNEL_SCHEDULER_SWITCH_OK','KERNEL_SCHEDULER_CLEANUP_OK','KERNEL_SCHEDULER_OK','KERNEL_SCHEDULER_WAIT_OK','KERNEL_SCHEDULER_TASK_IRQ_OK','KERNEL_SCHEDULER_IDLE_OK','KERNEL_SCHEDULER_PERSISTENT_OK','KERNEL_SCHEDULER_IDLE_REPEAT_OK','KERNEL_SCHEDULER_IRQ_BINDING_OK','KERNEL_SCHEDULER_PAYLOAD_OK')
        }
        if ($Fault -eq 'rollback') { $markers += 'KERNEL_SCHEDULER_ROLLBACK_OK' }
        foreach ($marker in $markers) {
            if (!$log.Contains($marker)) { throw "Missing $marker" }
        }
        if ($negative -and !$blocked -and !$timeout) {
            $cr2 = switch ($Fault) { 'guard-low' {($OsWindows + 0x140000)} 'guard-high' {($OsWindows + 0x151000)}
                default {($OsWindows + 0x141000)} }
            $errorCode = if ($Fault -eq 'nx') {17} else {0}
            if ((Read-Hex $log 'FAULT_VECTOR') -ne 14 -or (Read-Hex $log 'FAULT_CR2') -ne $cr2 -or
                (Read-Hex $log 'FAULT_ERROR') -ne $errorCode) { throw 'Unexpected fault vector/address/error' }
        } elseif (!$blocked -and ((Read-Hex $log 'SCHEDULER_BUDGET_BEFORE') -ne (Read-Hex $log 'SCHEDULER_BUDGET_AFTER') -or
            (Read-Hex $log 'SCHEDULER_DISPATCHES') -ne 210)) { throw 'Accounting/progress mismatch' }
        $pte = Read-Hex $log 'SCHEDULER_PTE'; $root = Read-Hex $log 'SCHEDULER_ROOT'
        if (!$negative) {
            $persistentSteps = Read-Hex $log 'SCHEDULER_PERSISTENT_DISPATCHES'
            if ((Read-Hex $log 'SCHEDULER_PERSISTENT_CYCLES') -ne 32 -or
                $persistentSteps -lt 64 -or $persistentSteps -gt 1024 -or
                (Read-Hex $log 'SCHEDULER_PERSISTENT_BUDGET_BEFORE') -ne (Read-Hex $log 'SCHEDULER_PERSISTENT_BUDGET_AFTER')) {
                throw 'Persistent stop/wait/progress/accounting mismatch'
            }
            $idleSlept = Read-Hex $log 'SCHEDULER_IDLE_SLEPT'
            if ((Read-Hex $log 'SCHEDULER_IDLE_DISPATCHES') -ne 6 -or
                (Read-Hex $log 'SCHEDULER_IDLE_RESUMED') -ne 3 -or $idleSlept -lt 1 -or $idleSlept -gt 9 -or
                (Read-Hex $log 'SCHEDULER_IDLE_BUDGET_BEFORE') -ne (Read-Hex $log 'SCHEDULER_IDLE_BUDGET_AFTER')) {
                throw 'Idle wake/progress/accounting mismatch'
            }
            $repeatSlept = Read-Hex $log 'SCHEDULER_IDLE_REPEAT_SLEPT'
            if ((Read-Hex $log 'SCHEDULER_IDLE_REPEAT_DISPATCHES') -ne 6 -or
                (Read-Hex $log 'SCHEDULER_IDLE_REPEAT_RESUMED') -ne 3 -or $repeatSlept -lt 1 -or $repeatSlept -gt 9 -or
                (Read-Hex $log 'SCHEDULER_IDLE_REPEAT_BUDGET_BEFORE') -ne (Read-Hex $log 'SCHEDULER_IDLE_REPEAT_BUDGET_AFTER')) {
                throw 'Repeated idle wake/progress/accounting mismatch'
            }
            $irqRip = Read-Hex $log 'SCHEDULER_IRQ_RIP'
            $irqRsp = Read-Hex $log 'SCHEDULER_IRQ_RSP'
            $irqFlags = Read-Hex $log 'SCHEDULER_IRQ_FLAGS'
            if ($irqRip -lt (Read-Hex $log 'SCHEDULER_IRQ_BEGIN') -or
                $irqRip -ge (Read-Hex $log 'SCHEDULER_IRQ_END') -or
                $irqRsp -lt ($OsWindows + 0x154000) -or $irqRsp -ge ($OsWindows + 0x163000) -or
                ($irqFlags -band 512) -ne 512 -or ($irqFlags -band 1024) -ne 0 -or
                (Read-Hex $log 'SCHEDULER_IRQ_TASK') -ne 1 -or
                (Read-Hex $log 'SCHEDULER_IRQ_GENERATION') -lt 2 -or
                (Read-Hex $log 'SCHEDULER_IRQ_DISPATCHES') -ne 3 -or
                (Read-Hex $log 'SCHEDULER_IRQ_BUDGET_BEFORE') -ne (Read-Hex $log 'SCHEDULER_IRQ_BUDGET_AFTER')) {
                throw 'Task IRQ frame/progress/accounting mismatch'
            }
        }
        Start-Sleep -Milliseconds 200
        $vm.StandardInput.WriteLine('stop'); $vm.StandardInput.WriteLine('info registers')
        $vm.StandardInput.WriteLine('x /144gx 0x' + $pte.ToString('x16'))
        $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Owned VM did not quit' }
        $monitor = $stdout.Result; $errors = $stderr.Result
        $monitor | Out-File "$scenario/monitor.log" -Encoding utf8
        $errors | Out-File "$scenario/stderr.log" -Encoding utf8
        $registers = [regex]::Match($monitor,'CR3=([0-9a-fA-F]{16})')
        $rsp = [regex]::Match($monitor,'RSP=([0-9a-fA-F]{16})')
        $cpuReady = if ($blocked -or $timeout) { $monitor -match 'RAX=5343484641494c01' }
        elseif ($negative) { $monitor -match 'RAX=4b4641554c540001' } else {
            $rsp.Success -and [Convert]::ToUInt64($rsp.Groups[1].Value,16) -ge ($OsWindows + 0x61000) -and
            [Convert]::ToUInt64($rsp.Groups[1].Value,16) -lt ($OsWindows + 0x71000) -and
            $monitor -match 'RAX=4b52454144590001'
        }
        if (!$registers.Success -or [Convert]::ToUInt64($registers.Groups[1].Value,16) -ne $root -or !$cpuReady) {
            throw 'CPU final state mismatch'
        }
        $words = @()
        foreach ($line in $monitor -split "`n") {
            if ($line -match '^([0-9a-fA-F]{16}):\s+(.*)$') {
                $address = [Convert]::ToUInt64($Matches[1],16)
                if ($address -ge $pte -and $address -lt $pte + 144 * 8) {
                    $words += [regex]::Matches($Matches[2],'0x([0-9a-fA-F]{16})') |
                        ForEach-Object { [Convert]::ToUInt64($_.Groups[1].Value,16) }
                }
            }
        }
        if ($words.Count -ne 144) { throw 'Missing task PTE evidence' }
        if (!$negative -or $Fault -eq 'retired') {
            if (@($words | Where-Object { $_ -ne 0 }).Count) { throw 'Task PTEs not absent' }
        } else {
            foreach ($slot in 0..7) {
                foreach ($offset in 0..17) {
                    $entry = $words[$slot * 18 + $offset]
                    if ($slot -lt 2 -and $offset -gt 0 -and $offset -lt 17) {
                        $base = $words[$slot * 18 + 1] -band 0x000FFFFFFFFFF000
                        $expected = ($base + ($offset - 1) * 4096) -bor $pteFlags
                        if (($entry -band $pteMask) -ne $expected) {
                            throw 'Live stack frame/permissions mismatch'
                        }
                    } elseif ($entry -ne 0) { throw 'Guard or unused task slot present' }
                }
            }
        }
        $trace = Get-Content $cpuLog -Raw
        if ($trace -match 'Triple fault') { throw 'Unexpected triple fault' }
        if (!$negative) {
            $hardware = [regex]::Matches($trace,'(?s)Servicing hardware INT=0x41\s+.*?RIP=([0-9a-fA-F]{16}) RFL=([0-9a-fA-F]+)')
            if ($hardware.Count -ne 3 -or
                [Convert]::ToUInt64($hardware[0].Groups[1].Value,16) -ne $irqRip -or
                [Convert]::ToUInt64($hardware[1].Groups[1].Value,16) -ne (Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME') -or
                [Convert]::ToUInt64($hardware[2].Groups[1].Value,16) -ne (Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME') -or
                !([Convert]::ToUInt64($hardware[2].Groups[2].Value,16) -band 512) -or
                !([Convert]::ToUInt64($hardware[1].Groups[2].Value,16) -band 512) -or
                !([Convert]::ToUInt64($hardware[0].Groups[2].Value,16) -band 512)) {
                throw 'Hardware trace does not confirm task IRQ'
            }
        }
        Write-Output "PASS: scheduler QEMU $Fault boot $bootIndex/$Boots, CPU/memory evidence verified"
    } finally {
        if ($vm.Id -and !$vm.HasExited) {
            try {
                $vm.StandardInput.WriteLine('stop'); $vm.StandardInput.WriteLine('info registers')
                $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
                if ($vm.WaitForExit(3000)) {
                    $stdout.Result | Out-File "$scenario/failure-monitor.log" -Encoding utf8
                    $stderr.Result | Out-File "$scenario/failure-stderr.log" -Encoding utf8
                }
            } finally { if (!$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() } }
        }
        $vm.Dispose()
    }
}
Write-Output "PASS: scheduler firmware evidence $out"
