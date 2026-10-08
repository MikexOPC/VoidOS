param([Parameter(Mandatory)][string]$ImageDirectory,
    [string]$QemuDirectory = 'C:/Program Files/qemu')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
. (Join-Path $PSScriptRoot 'firmware.ps1')
$out = Join-Path $project ('kernel/build/lapic-calibration-firmware-' + [guid]::NewGuid().ToString('N'))
$boot = Join-Path $out 'esp/EFI/BOOT'
New-Item -ItemType Directory -Path $boot -Force | Out-Null
Copy-Item (Join-Path $ImageDirectory 'kernel.elf') $boot
Copy-Item (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI') $boot
[IO.File]::WriteAllText("$boot/boot-check.txt","OS boot check`r`n")
Get-FileHash "$boot/kernel.elf", "$boot/BOOTX64.EFI" |
    ConvertTo-Json | Out-File "$out/images.json" -Encoding utf8

# ============ mandatory hexadecimal field ============

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text,$Pattern)
    if (!$match.Success) { throw "Missing field: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}

# ============ ten boots with actual LAPIC ownership and MMIO state ============

for ($index = 1; $index -le 10; ++$index) {
    $serial = Join-Path $out "serial-$index.log"
    $debug = Join-Path $out "debug-$index.log"
    $variables = Join-Path $out "vars-$index.fd"
    Copy-Item (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') $variables
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m','128',
        '-net','none','-display','none','-monitor','stdio','-no-reboot',
        '-d','int,cpu_reset','-D',$debug,'-serial',('file:'+$serial),
        '-drive',('if=pflash,format=raw,readonly=on,file='+$QemuDirectory+'/share/edk2-x86_64-code.fd'),
        '-drive',('if=pflash,format=raw,file='+$variables),
        '-drive',('format=raw,file=fat:rw:'+ (Join-Path $out 'esp')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
    $start.Arguments = ($arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new(); $vm.StartInfo = $start
    $started = $false; $passed = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU failed to start' }
        $stderr = $vm.StandardError.ReadToEndAsync()
        $stdout = $vm.StandardOutput.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($vm.HasExited) { throw 'QEMU exited early' }
            if (Test-Path $serial) {
                $stream = [IO.File]::Open($serial,'Open','Read','ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
                if ($log -match '\[ FAIL \]|KERNEL PANIC|KERNEL_EXCEPTION') { throw 'Kernel failure' }
                if ($log.Contains('KERNEL_READY')) { $passed = $true; break }
            }
            Start-Sleep -Milliseconds 50
        }
        if (!$passed) { throw 'Kernel timeout' }
        $attempt = Read-Hex $log 'KERNEL_TIMER_CALIBRATION_ATTEMPT: 0x([0-9a-f]{16})'
        if ($attempt -lt 1 -or $attempt -gt 3) { throw 'Invalid calibration attempt' }
        $vm.StandardInput.WriteLine('stop')
        Write-LapicTimerMonitor $log $vm
        $vm.StandardInput.WriteLine('info lapic')
        $vm.StandardInput.Flush()
        Start-Sleep -Milliseconds 300
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        if ($started) {
            $monitor = $stdout.GetAwaiter().GetResult()
            [IO.File]::WriteAllText("$out/monitor-$index.log",$monitor)
            [IO.File]::WriteAllText("$out/stderr-$index.log",$stderr.GetAwaiter().GetResult())
        }
        $vm.Dispose()
    }
    if ((Get-Content -LiteralPath $debug -Raw) -match '(?i)triple fault') { throw 'Triple fault' }
    Test-LapicTimerFirmware $log $monitor $debug
    if ($index -eq 1) { Test-LapicTimerMonitorRejections $log $monitor $debug }
    Write-Output "LAPIC boot $index/10: calibration, IRQ, HLT, stop and restore verified"
}
Write-Output "Evidence: $out"
