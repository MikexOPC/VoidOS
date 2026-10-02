param(
    [string]$ToolchainBin,
    [string]$QemuDirectory = 'C:\Program Files\qemu',
    [ValidateRange(5, 60)][int]$TimeoutSeconds = 25
)
$ErrorActionPreference = 'Stop'
$bootRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$projectRoot = (Resolve-Path (Join-Path $bootRoot '..\..\..\..')).Path
. (Join-Path $bootRoot 'scripts\status.ps1')

# ====== toolchain e artefatti del bootloader gia verificati ====== #

if (!$ToolchainBin) {
    $base = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
    $version = Get-ChildItem -LiteralPath $base -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (!$version) { throw 'Toolchain MSVC x64 mancante' }
    $ToolchainBin = Join-Path $version.FullName 'bin\Hostx64\x64'
}
$compilerInclude = (Resolve-Path (Join-Path $ToolchainBin '..\..\..\include')).Path
$output = Join-Path $bootRoot 'build'
$qemu = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$code = Join-Path $QemuDirectory 'share\edk2-x86_64-code.fd'
$template = Join-Path $QemuDirectory 'share\edk2-i386-vars.fd'
$production = Join-Path $output 'esp\EFI\BOOT\BOOTX64.EFI'
foreach ($required in @($qemu, $code, $template, $production,
                        "$output\log.obj", "$output\exception_entries.obj", "$output\x86_descriptors.lib")) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "File mancante: $required" }
}
$productionHash = (Get-FileHash -LiteralPath $production -Algorithm SHA256).Hash
$testRoot = Join-Path $output ('exception-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Write-BootSection 'eccezioni reali in QEMU con immagine dedicata'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Errore in $Program ($LASTEXITCODE)" }
}
Invoke-Checked (Join-Path $ToolchainBin 'ml64.exe') @('/nologo', '/W3', '/WX', '/c',
    "/Fo$testRoot\trigger.obj", (Join-Path $PSScriptRoot 'assembly\exceptions\trigger.asm'))

# ====== errori CPU veri con e senza error code ====== #

foreach ($vector in @(0, 6, 13)) {
    $scenarioRoot = Join-Path $testRoot "vector-$vector"
    $esp = Join-Path $scenarioRoot 'esp'
    $bootDirectory = Join-Path $esp 'EFI\BOOT'
    New-Item -ItemType Directory -Path $bootDirectory | Out-Null
    Invoke-Checked (Join-Path $ToolchainBin 'cl.exe') @('/nologo', '/TC', '/std:c11', '/W4', '/WX',
        '/GS-', '/Zl', '/Od', '/c', "/DBOOT_TEST_EXCEPTION_VECTOR=$vector", "/I$bootRoot",
        "/I$projectRoot", "/I$compilerInclude", (Join-Path $PSScriptRoot 'exception_probe.c'),
        "/Fo$scenarioRoot\probe.obj")
    $image = Join-Path $bootDirectory 'BOOTX64.EFI'
    Invoke-Checked (Join-Path $ToolchainBin 'link.exe') @('/nologo', '/machine:x64',
        '/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib', '/incremental:no',
        '/dynamicbase:no', '/fixed:no', "/out:$image", "$scenarioRoot\probe.obj", "$testRoot\trigger.obj",
        "$output\exception_entries.obj", "$output\x86_descriptors.lib", "$output\os_memory.lib", "$output\log.obj")
    Write-BootStatus INFO "Probe vettore $vector SHA256: $((Get-FileHash -LiteralPath $image).Hash)"
    $variables = Join-Path $scenarioRoot 'vars.fd'
    Copy-Item -LiteralPath $template -Destination $variables
    $serial = Join-Path $scenarioRoot 'serial.log'
    $arguments = @('-machine', 'q35', '-accel', 'tcg', '-m', '128', '-net', 'none',
        '-display', 'none', '-monitor', 'stdio', '-no-reboot',
        '-serial', ('file:' + $serial.Replace('\', '/')),
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + $code.Replace('\', '/')),
        '-drive', ('if=pflash,format=raw,file=' + $variables.Replace('\', '/')),
        '-drive', ('format=raw,file=fat:rw:' + $esp.Replace('\', '/')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $qemu
    $start.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new()
    $vm.StartInfo = $start
    $started = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'Avvio QEMU fallito' }
        $stdout = $vm.StandardOutput.ReadToEndAsync()
        $stderr = $vm.StandardError.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $ready = $false
        $log = ''
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $serial) {
                $stream = [IO.File]::Open($serial, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
                $log = [regex]::Replace($log, '\x1b\[[0-?]*[ -/]*[@-~]', '')
            }
            if ($log.Contains('EXCEPTION_PROBE_READY')) { $ready = $true; break }
            if ($vm.HasExited) { throw 'QEMU terminato prima della prova' }
            Start-Sleep -Milliseconds 100
        }
        if (!$ready) { throw "Timeout probe vettore $vector" }
        $access = [regex]::Match($log, 'TSS_ACCESS: 0x([0-9A-F]{16})')
        if (!$access.Success) { throw 'Indirizzo descrittore TSS assente' }
        Start-Sleep -Milliseconds 200
        $vm.StandardInput.WriteLine('stop')
        $vm.StandardInput.WriteLine('info registers')
        $vm.StandardInput.WriteLine('x /8gx $rsp')
        $vm.StandardInput.WriteLine('x /1bx 0x' + $access.Groups[1].Value)
        $vm.StandardInput.WriteLine('quit')
        $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor QEMU non risponde' }
        $registers = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $scenarioRoot 'registers.log'), $registers)
        [IO.File]::WriteAllText((Join-Path $scenarioRoot 'stderr.log'), $stderr.GetAwaiter().GetResult())
        $stop = [regex]::Match($log, 'EXCEPTION_STOP: 0x([0-9A-F]{16})')
        $rax = [regex]::Match($registers, 'RAX=([0-9A-Fa-f]{16})')
        $rip = [regex]::Match($registers, 'RIP=([0-9A-Fa-f]{16})')
        $rsp = [regex]::Match($registers, 'RSP=([0-9A-Fa-f]{16})')
        $stack = [regex]::Match($log, 'EXCEPTION_STACK: 0x([0-9A-F]{16})')
        $flags = [regex]::Match($registers, 'RFL=([0-9A-Fa-f]{8})')
        $frame = [regex]::Match($registers, '(?m)^[0-9A-Fa-f]{16}:\s+0x([0-9A-Fa-f]{16})')
        if (!$stop.Success -or !$rax.Success -or !$rip.Success -or !$rsp.Success -or !$stack.Success -or
            [Convert]::ToUInt64($rsp.Groups[1].Value, 16) -lt [Convert]::ToUInt64($stack.Groups[1].Value, 16) -or
            [Convert]::ToUInt64($rsp.Groups[1].Value, 16) -ge [Convert]::ToUInt64($stack.Groups[1].Value, 16) + 16384 -or
            !$flags.Success -or ([Convert]::ToUInt64($flags.Groups[1].Value, 16) -band 512) -ne 0 -or
            [Convert]::ToUInt64($rax.Groups[1].Value, 16) -ne $vector -or
            [Convert]::ToUInt64($rip.Groups[1].Value, 16) -ne [Convert]::ToUInt64($stop.Groups[1].Value, 16) + 2 -or
            $registers -notmatch 'HLT=1' -or $registers -notmatch 'CS =0008' -or
            $registers -notmatch 'TR =0018' -or
            $registers -notmatch ('(?im)^' + $access.Groups[1].Value + ':\s+0x8b\b') -or
            $registers -notmatch 'GDT=\s+[0-9A-Fa-f]+\s+00000027' -or
            $registers -notmatch 'IDT=\s+[0-9A-Fa-f]+\s+000001ff') {
            throw "Vettore $vector non arrestato nel nostro handler: $scenarioRoot"
        }
        if ($vector -eq 13 -and (!$frame.Success -or
            [Convert]::ToUInt64($frame.Groups[1].Value, 16) -ne 40)) {
            throw "Error code #GP non conservato: $scenarioRoot"
        }
        Write-BootStatus OK "Eccezione $vector`: TSS attivo, stack IST1, RAX, RIP, IF=0 e HLT verificati nella CPU emulata."
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        $vm.Dispose()
    }
}
if ((Get-FileHash -LiteralPath $production -Algorithm SHA256).Hash -ne $productionHash) {
    throw 'Immagine bootloader modificata dal test'
}
Write-BootStatus INFO "Log: $testRoot"
Write-BootStatus OK '3/3 eccezioni CPU reali verificate; bootloader ordinario invariato.'
