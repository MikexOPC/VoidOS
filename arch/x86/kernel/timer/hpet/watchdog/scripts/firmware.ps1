param([Parameter(Mandatory)][string]$ImageDirectory,
    [ValidateSet('normal','lost','absent')][string]$Mode = 'normal',
    [ValidateRange(1,100)][int]$Repetitions = 10,
    [ValidateRange(0,31)][int]$ExpectedTimer=0,
    [ValidateRange(16,23)][int]$ExpectedPin=16,
    [switch]$CleanupRecovery,
    [string]$QemuDirectory = 'C:/Program Files/qemu')
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../../../..')).Path
. (Join-Path $project 'kernel/test/timer/lapic/scripts/firmware.ps1')
$out = Join-Path $project ('kernel/build/watchdog-' + $Mode + '-' + [guid]::NewGuid().ToString('N'))
$boot = Join-Path $out 'esp/EFI/BOOT'
New-Item -ItemType Directory -Path $boot -Force | Out-Null
Copy-Item (Join-Path $ImageDirectory 'kernel.elf') $boot
Copy-Item (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI') $boot
[IO.File]::WriteAllText("$boot/boot-check.txt","OS boot check`r`n")
Get-FileHash "$boot/kernel.elf", "$boot/BOOTX64.EFI" |
    ConvertTo-Json | Out-File "$out/images.json" -Encoding utf8
Get-ChildItem $module -Recurse -File | ForEach-Object { Get-FileHash $_.FullName } |
    ConvertTo-Json | Out-File "$out/source-manifest.json" -Encoding utf8

# ============ mandatory fields and closed owner ============

. (Join-Path $PSScriptRoot 'validation.ps1')
$cleanup = if ($CleanupRecovery) {20} else {0}
$attempts = if ($CleanupRecovery) {1} else {0}

# ============ actual HPET and LAPIC interrupts under a host deadline ============

for ($index = 1; $index -le $Repetitions; ++$index) {
    $serial = Join-Path $out "serial-$index.log"
    $debug = Join-Path $out "debug-$index.log"
    $variables = Join-Path $out "vars-$index.fd"
    Copy-Item (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') $variables
    $machine = if ($Mode -eq 'absent') { 'q35,hpet=off' } else { 'q35' }
    $arguments = @('-machine',$machine,'-cpu','max','-accel','tcg','-m','128',
        '-net','none','-display','none','-monitor','stdio','-no-reboot',
        '-d','int,cpu_reset','-D',$debug,'-serial',('file:'+$serial),
        '-drive',('if=pflash,format=raw,readonly=on,file='+$QemuDirectory+'/share/edk2-x86_64-code.fd'),
        '-drive',('if=pflash,format=raw,file='+$variables),
        '-drive',('format=raw,file=fat:rw:'+(Join-Path $out 'esp')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
    $start.Arguments = ($arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new(); $vm.StartInfo = $start
    $started = $false; $passed = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU failed to start' }
        $stderr = $vm.StandardError.ReadToEndAsync(); $stdout = $vm.StandardOutput.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($vm.HasExited) { throw 'QEMU exited early' }
            if (Test-Path $serial) {
                $stream = [IO.File]::Open($serial,'Open','Read','ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
                if ($log -match 'KERNEL_EXCEPTION') { throw 'Unexpected exception' }
                if ($Mode -eq 'normal') {
                    if ($log -match '\[ FAIL \]|KERNEL PANIC') { throw 'Kernel failure' }
                    if ($log.Contains('KERNEL_READY')) { $passed = $true; break }
                } elseif ($log.Contains('kernel panic') -and $log.Contains('LAPIC_TIMER_INVALID')) {
                    if (!$log.Contains('LAPIC_TIMER_INVALID') -or $log.Contains('KERNEL_READY')) {
                        throw 'Unexpected negative result'
                    }
                    $passed = $true; break
                }
            }
            Start-Sleep -Milliseconds 50
        }
        if (!$passed) { throw 'Kernel timeout: independent watchdog did not terminate wait' }
        $vm.StandardInput.WriteLine('stop')
        $vm.StandardInput.WriteLine('info registers'); $vm.StandardInput.WriteLine('info lapic')
        if ($Mode -ne 'absent') {
            $owner = Read-Hex $log 'KERNEL_WATCHDOG_OWNER: 0x([0-9a-f]{16})'
            $vm.StandardInput.WriteLine('x /30gx 0x'+($owner -band -16L).ToString('x16'))
            Write-LapicTimerMonitor $log $vm
        }
        $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor failed to stop owned VM' }
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        if ($started) {
            $monitor = $stdout.GetAwaiter().GetResult()
            [IO.File]::WriteAllText("$out/monitor-$index.log",$monitor)
            [IO.File]::WriteAllText("$out/stderr-$index.log",$stderr.GetAwaiter().GetResult())
        }
        $vm.Dispose()
    }
    $trace = Get-Content -LiteralPath $debug -Raw
    if ($trace -match '(?i)triple fault' -or $monitor -notmatch 'HLT=1' -or
        $monitor -notmatch '(?im)^ISR\s+\(none\)' -or $monitor -notmatch '(?im)^IRR\s+\(none\)' -or
        $monitor -notmatch '(?im)^LVTT\s+0x00010040' -or
        $monitor -notmatch '(?im)^Timer\s+DCR=0xb\s+.*initial_count = 0 current_count = 0' -or
        ((Read-Hex $monitor 'RFL=([0-9a-f]+)') -band 512)) { throw 'Fault or source not stopped/restored' }
    $lapicEvents = [regex]::Matches($trace,'Servicing hardware INT=0x41').Count
    if ($Mode -eq 'normal') {
        Test-LapicTimerFirmware $log $monitor $debug
        Test-WatchdogOwner $log $monitor 3 1 $ExpectedTimer $ExpectedPin $cleanup $attempts
    } elseif ($Mode -eq 'lost') {
        if ($lapicEvents -ne 0 -or !$log.Contains('TIMER_WATCHDOG_TIMEOUT_CLEAN')) { throw 'Lost IRQ not recovered' }
        Test-WatchdogOwner $log $monitor 1 2 $ExpectedTimer $ExpectedPin $cleanup $attempts
        $source = Read-Hex $log 'KERNEL_TIMER_OWNER: 0x([0-9a-f]{16})'
        if ((Read-TimerWord $monitor $source) -ne 0 -or
            (Read-TimerWord $monitor ($source+80)) -ne 0 -or
            (Read-TimerWord $monitor ($source+88)) -ne 0 -or
            (Read-TimerWord $monitor ($source+136)) -ne (4L -shl 32)) { throw 'Lost LAPIC owner not closed' }
    } else {
        if ($lapicEvents -or !$log.Contains('KERNEL_HPET_ABSENT') -or $log.Contains('KERNEL_WATCHDOG_READY')) {
            throw 'Missing HPET entered timer wait'
        }
    }
    if ($Mode -ne 'absent') {
        $resume = Read-Hex $log 'KERNEL_TIMER_SLEEP_RESUME: 0x([0-9a-f]{16})'
        $events = [regex]::Matches($trace,'(?s)Servicing hardware INT=0x40\s+.*?RIP=([0-9a-f]{16}) RFL=([0-9a-f]+)')
        $watchdogEvents = @($events | Where-Object { [Convert]::ToUInt64($_.Groups[1].Value,16) -eq $resume })
        $expected = if ($Mode -eq 'normal') { 3 } else { 1 }
        if ($watchdogEvents.Count -ne $expected -or
            $monitor -notmatch ('(?im)^'+($resume-2).ToString('x16')+':\s+0xfb\s+0xf4\s+0xfa')) {
            throw 'Independent IRQ count or STI HLT CLI instruction sequence missing'
        }
        foreach ($event in $watchdogEvents) {
            if (!([Convert]::ToUInt64($event.Groups[2].Value,16) -band 512)) { throw 'HPET interrupt with IF=0' }
        }
    }
    Write-Output "Watchdog $Mode boot $index/$Repetitions verified"
}
Write-Output "Evidence: $out"
