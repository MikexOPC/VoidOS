param([string]$QemuDirectory = 'C:\Program Files\qemu')
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../../../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
$out = Join-Path $project ('kernel/build/hpet-kernel-' + [guid]::NewGuid().ToString('N'))
$image = Join-Path $out 'image'
New-Item -ItemType Directory -Path $image | Out-Null
$sources = @(Get-ChildItem $module -Recurse -File | Where-Object Extension -In '.c','.h','.rs','.ps1') +
    @(Get-Item (Join-Path $project 'kernel/main.c'), (Join-Path $project 'kernel/scripts/build.ps1'))
$manifest = $sources | ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 }
$manifest | ConvertTo-Json | Out-File "$out/source-manifest.json" -Encoding utf8
& (Join-Path $project 'kernel/scripts/build.ps1') -OutputDirectory $image |
    Tee-Object -FilePath "$out/build.log"
$esp = Join-Path $out 'esp'
$boot = Join-Path $esp 'EFI/BOOT'
New-Item -ItemType Directory -Path $boot | Out-Null
Copy-Item (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI') $boot
Copy-Item (Join-Path $image 'kernel.elf') $boot
[IO.File]::WriteAllText("$boot/boot-check.txt", "OS boot check`r`n")
Get-FileHash "$boot/BOOTX64.EFI", "$boot/kernel.elf" -Algorithm SHA256 |
    ConvertTo-Json | Out-File "$out/image-manifest.json" -Encoding utf8

# ============ real kernel and firmware ACPI discovery ============

function Read-Number([string]$Log, [string]$Name) {
    $match = [regex]::Match($Log, [regex]::Escape($Name) + ': 0x([0-9a-f]{16})')
    if (!$match.Success) { throw "Missing field: $Name" }
    return [Convert]::ToUInt64($match.Groups[1].Value, 16)
}

Write-BootSection 'HPET collegato al kernel: dieci boot e un profilo senza HPET'
for ($index = 1; $index -le 11; ++$index) {
    $absent = $index -eq 11
    $serial = Join-Path $out "serial-$index.log"
    $debug = Join-Path $out "debug-$index.log"
    $variables = Join-Path $out "vars-$index.fd"
    Copy-Item (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') $variables
    $machine = if ($absent) { 'q35,hpet=off' } else { 'q35' }
    $arguments = @('-machine', $machine, '-cpu', 'max', '-accel', 'tcg', '-m', '128',
        '-net', 'none', '-display', 'none', '-monitor', 'stdio', '-no-reboot',
        '-d', 'int,cpu_reset', '-D', $debug, '-serial', ('file:' + $serial),
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + $QemuDirectory + '/share/edk2-x86_64-code.fd'),
        '-drive', ('if=pflash,format=raw,file=' + $variables),
        '-drive', ('format=raw,file=fat:rw:' + $esp))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
    $start.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardInput = $true
    $vm = [Diagnostics.Process]::new()
    $vm.StartInfo = $start
    $started = $false
    $passed = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU start failed' }
        $stderr = $vm.StandardError.ReadToEndAsync()
        $stdout = $vm.StandardOutput.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($vm.HasExited) { throw "QEMU exited: attempt $index" }
            if (Test-Path $serial) {
                $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
                if ($log -match 'KERNEL_EXCEPTION') { throw "Kernel exception: $index" }
                if ($absent -and $log.Contains('kernel panic') -and $log.Contains('LAPIC_TIMER_INVALID')) {
                    $passed = $true; break
                }
                if (!$absent -and $log -match '\[ FAIL \]|KERNEL PANIC') { throw "Kernel failure: $index" }
                if (!$absent -and $log.Contains('KERNEL_READY')) { $passed = $true; break }
            }
            Start-Sleep -Milliseconds 50
        }
        if (!$passed) { throw "Kernel timeout: $index" }
        if ($absent) {
            if (!$log.Contains('KERNEL_HPET_ABSENT') || $log.Contains('KERNEL_HPET_DISCOVERY_OK') ||
                $log.Contains('KERNEL_HPET_PROBE_OK') || $log.Contains('KERNEL_WATCHDOG_READY') ||
                $log.Contains('KERNEL_READY')) {
                throw 'Absent HPET profile reported a successful discovery'
            }
        } else {
            if (!$log.Contains('KERNEL_HPET_DISCOVERY_OK') || $log.Contains('KERNEL_HPET_ABSENT')) {
                throw 'HPET discovery marker missing'
            }
            if (!$log.Contains('KERNEL_HPET_PROBE_OK') ||
                (Read-Number $log 'KERNEL_HPET_PERIOD_FS') -lt 1 -or
                (Read-Number $log 'KERNEL_HPET_PERIOD_FS') -gt 100000000) {
                throw 'Read-only HPET probe not verified'
            }
            $owner = Read-Number $log 'KERNEL_HPET_DESCRIPTION'
            if (!$log.Contains('KERNEL_HPET_CANDIDATE_OBSERVED') ||
                (Read-Number $log 'KERNEL_HPET_CANDIDATE') -ge
                (Read-Number $log 'KERNEL_HPET_COMPARATORS') -or
                ((Read-Number $log 'KERNEL_HPET_ROUTE_MASK') -eq 0 -and
                (Read-Number $log 'KERNEL_HPET_FSB_CAPABLE') -eq 0)) {
                throw 'No usable HPET comparator capability observed'
            }
            if (($owner -band 7) -or $owner -lt 0x100000000 -or $owner -ge 0x100100000) {
                throw 'Persistent HPET description outside kernel image'
            }
            $vm.StandardInput.WriteLine('stop')
            $vm.StandardInput.WriteLine('info registers')
            $vm.StandardInput.WriteLine('x /2gx 0x' + $owner.ToString('x16'))
            $vm.StandardInput.Flush()
            Start-Sleep -Milliseconds 250
        }
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        if ($started) {
            [IO.File]::WriteAllText("$out/stderr-$index.log", $stderr.GetAwaiter().GetResult())
            $monitor = $stdout.GetAwaiter().GetResult()
            [IO.File]::WriteAllText("$out/monitor-$index.log", $monitor)
        }
        $vm.Dispose()
    }
    if ([IO.File]::ReadAllText($debug) -match '(?i)triple fault') { throw "Triple fault: $index" }
    if (!$absent) {
        $row = [regex]::Match($monitor, '(?im)^' + $owner.ToString('x16') +
            ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
        if (!$row.Success -or [Convert]::ToUInt64($row.Groups[1].Value,16) -ne
            (Read-Number $log 'KERNEL_HPET_PHYSICAL') -or
            ([Convert]::ToUInt64($row.Groups[2].Value,16) -band 4294967295L) -ne
            (Read-Number $log 'KERNEL_HPET_BLOCK_ID')) { throw 'Persisted description mismatch' }
    }
    Write-BootStatus OK "Kernel boot $index/11: HPET absent=$absent, esito del profilo verificato."
}
foreach ($source in $manifest) {
    if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Hash) {
        throw "Source changed during tests: $($source.Path)"
    }
}
Write-BootStatus INFO "Evidenze: $out"
