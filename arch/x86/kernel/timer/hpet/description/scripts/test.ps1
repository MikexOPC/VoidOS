param([string]$QemuDirectory = 'C:\Program Files\qemu')
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../../../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
$out = Join-Path $project ('kernel/build/hpet-description-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$manifest = Get-ChildItem $module -Recurse -File |
    Where-Object Extension -In '.c', '.h', '.rs', '.ps1' |
    ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 }
$manifest | ConvertTo-Json | Out-File "$out/source-manifest.json" -Encoding utf8
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem ($sdk + '/Include') -Directory |
    Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
$savedPath = $env:PATH

# ============ checked tool execution ============

. (Join-Path $PSScriptRoot '../../../../../../../scripts/lib/common.ps1')
function Invoke-Checked([string]$Program, [string[]]$Arguments) { Invoke-OsChecked $Program $Arguments "$out/tools.log" -Tee }

try {
    $bin = $vc.FullName + '/bin/Hostx64/x64'
    $env:PATH = $bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $compiler = $bin + '/cl.exe'
    $rustc = (Get-Command rustc -ErrorAction Stop).Source

    # ============ Rust policy and C ABI tests ============

    Write-BootSection 'HPET description: Rust, C e ASan'
    Invoke-Checked $rustc @('--edition=2024', '--crate-type=lib', '--emit=obj',
        '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings',
        (Join-Path $module 'rust/validation/fields.rs'), '-o', "$out/fields.obj")
    Invoke-Checked $rustc @('--edition=2024', '--test', '-D', 'warnings',
        (Join-Path $module '../../../../../../test/arch/x86/kernel/timer/hpet/description/rust/validation/fields_test.rs'), '-o', "$out/rust-tests.exe")
    Invoke-Checked "$out/rust-tests.exe" @()
    $runner = @'
#include <stdio.h>
int run_hpet_description_tests(void);
int run_hpet_scan_tests(void);
int run_hpet_kernel_tests(void);
int main(void) {
    unsigned i;
    for (i = 0; i < 100; ++i) {
        int result = run_hpet_description_tests();
        if (result) { printf("HPET case %d failed\n", result); return 1; }
        result = run_hpet_scan_tests();
        if (result) { printf("HPET scan case %d failed\n", result); return 1; }
        result = run_hpet_kernel_tests();
        if (result) { printf("HPET kernel adapter case %d failed\n", result); return 1; }
    }
    puts("HPET_DESCRIPTION_ASAN_PASS: 100 repetitions");
    return 0;
}
'@
    [IO.File]::WriteAllText("$out/runner.c", $runner, [Text.UTF8Encoding]::new($false))
    $objects = @()
    foreach ($source in @('../../../../../../test/kernel/mm/physical/synchronization/reader_model.c', 'table.c', 'scan.c', 'integration/kernel.c',
        '../../../../../../test/arch/x86/kernel/timer/hpet/description/table_test.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/scan_test.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/integration/kernel_test.c')) {
        $object = Join-Path $out ($source.Replace('/', '-') + '-asan.obj')
        Invoke-Checked $compiler @('/nologo', '/std:c11', '/W4', '/WX', '/TC', '/c',
            '/fsanitize=address', '/Zi', '/MD', '/Od', ("/I" + $project), "/Fd$out/asan.pdb", "/Fo$object",
            (Join-Path $module $source))
        $objects += $object
    }
    $arguments = @('/nologo', '/std:c11', '/W4', '/WX', '/fsanitize=address', '/Zi', '/MD',
        "/Tc$out/runner.c", "/Fo$out/runner.obj", "/Fd$out/asan.pdb", "/Fe$out/tests.exe") +
        $objects + @("$out/fields.obj", '/link', '/incremental:no')
    Invoke-Checked $compiler $arguments
    Copy-Item ($bin + '/clang_rt.asan_dynamic-x86_64.dll') $out
    Invoke-Checked "$out/tests.exe" @()
    $probeRunner = @'
#include <stdio.h>
int run_hpet_probe_tests(void);
int main(void) {
    unsigned i;
    for (i = 0; i < 100; ++i) {
        int result = run_hpet_probe_tests();
        if (result) { printf("HPET probe case %d failed\n", result); return 1; }
    }
    puts("HPET_PROBE_ASAN_PASS: 100 repetitions");
    return 0;
}
'@
    [IO.File]::WriteAllText("$out/probe-runner.c", $probeRunner, [Text.UTF8Encoding]::new($false))
    $probeObjects = @()
    foreach ($source in @('../../../../../../test/kernel/mm/physical/synchronization/reader_model.c', 'probe/policy.c', 'probe/hardware.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/probe/probe_test.c')) {
        $object = Join-Path $out ($source.Replace('/', '-') + '-asan.obj')
        Invoke-Checked $compiler @('/nologo', '/std:c11', '/W4', '/WX', '/TC', '/c',
            '/fsanitize=address', '/Zi', '/MD', '/Od', ("/I" + $project), "/Fd$out/asan.pdb", "/Fo$object",
            (Join-Path $module $source))
        $probeObjects += $object
    }
    $arguments = @('/nologo', '/std:c11', '/W4', '/WX', '/fsanitize=address', '/Zi', '/MD',
        "/Tc$out/probe-runner.c", "/Fo$out/probe-runner.obj", "/Fd$out/asan.pdb", "/Fe$out/probe-tests.exe") +
        $probeObjects + @("$out/fields.obj", '/link', '/incremental:no')
    Invoke-Checked $compiler $arguments
    Invoke-Checked "$out/probe-tests.exe" @()
    Write-BootStatus OK 'Controlli Rust e 100 ripetizioni delle suite C decoder/discovery con ASan superati.'

    # ============ freestanding UEFI component harness ============

    $objects = @()
    foreach ($source in @('table.c', 'scan.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/table_test.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/scan_test.c', '../../../../../../test/arch/x86/kernel/timer/hpet/description/uefi/entry.c')) {
        $object = Join-Path $out ($source.Replace('/', '-') + '-efi.obj')
        Invoke-Checked $compiler @('/nologo', '/std:c11', '/W4', '/WX', '/TC', '/c',
            '/GS-', '/Zl', '/Od', ("/I" + $project), "/Fo$object", (Join-Path $module $source))
        $objects += $object
    }
    $esp = Join-Path $out 'esp'
    $boot = Join-Path $esp 'EFI/BOOT'
    New-Item -ItemType Directory -Path $boot | Out-Null
    $arguments = @('/nologo', '/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib',
        '/machine:x64', '/incremental:no', "/out:$boot/BOOTX64.EFI") + $objects + @("$out/fields.obj")
    Invoke-Checked ($bin + '/link.exe') $arguments

    # ============ ten independent UEFI starts ============

    Write-BootSection 'HPET description: dieci esecuzioni UEFI del componente'
    for ($index = 1; $index -le 10; ++$index) {
        $serial = Join-Path $out "serial-$index.log"
        $debug = Join-Path $out "debug-$index.log"
        $variables = Join-Path $out "vars-$index.fd"
        Copy-Item (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') $variables
        $arguments = @('-machine', 'q35', '-cpu', 'max', '-accel', 'tcg', '-m', '128',
            '-net', 'none', '-display', 'none', '-monitor', 'none', '-no-reboot',
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
        $vm = [Diagnostics.Process]::new()
        $vm.StartInfo = $start
        $started = $false
        $passed = $false
        try {
            $started = $vm.Start()
            if (!$started) { throw 'QEMU start failed' }
            $stderr = $vm.StandardError.ReadToEndAsync()
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            while ([DateTime]::UtcNow -lt $deadline) {
                if ($vm.HasExited) { throw "QEMU exited: attempt $index" }
                if (Test-Path $serial) {
                    $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                    $reader = [IO.StreamReader]::new($stream)
                    try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
                    if ($text.Contains('HPET_DESCRIPTION_TEST_FAILED')) { throw "Test failure: $index" }
                    if ($text.Contains('HPET_DESCRIPTION_TEST_READY')) { $passed = $true; break }
                }
                Start-Sleep -Milliseconds 50
            }
            if (!$passed) { throw "UEFI test timeout: $index" }
        } finally {
            if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
            if ($started) {
                [IO.File]::WriteAllText("$out/stderr-$index.log", $stderr.GetAwaiter().GetResult())
            }
            $vm.Dispose()
        }
        if ([IO.File]::ReadAllText($debug) -match '(?i)triple fault') { throw "Triple fault: $index" }
        Write-BootStatus OK "UEFI test $index/10: decoder e discovery C/Rust verificati su fixture."
    }
    foreach ($source in $manifest) {
        if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Hash) {
            throw "Source changed during tests: $($source.Path)"
        }
    }
    Write-BootStatus INFO "Evidenze: $out"
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
    $env:PATH = $savedPath
}
