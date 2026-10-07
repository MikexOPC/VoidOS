param([string]$QemuDirectory = 'C:\Program Files\qemu')
$ErrorActionPreference = 'Stop'
$bootRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$projectRoot = (Resolve-Path (Join-Path $bootRoot '../../../..')).Path
. (Join-Path $bootRoot 'scripts/status.ps1')
$out = Join-Path $bootRoot ('build/elf-validation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null

# ============ ELF HOST ADDRESS SANITIZER ============

Write-BootSection 'ELF host ASan e rollback'
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem ($sdk + '/Include') -Directory |
    Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
try {
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $compiler = $vc.FullName + '/bin/Hostx64/x64/cl.exe'
    $compileFlags = @('/nologo', '/W4', '/WX', '/TC', '/c', '/fsanitize=address',
        '/Zi', '/MD', '/Od', ('/I' + $bootRoot), ('/I' + $projectRoot),
        ('/Fd' + $out + '/asan.pdb'))
    $objects = @()
    foreach ($source in @('elf/load.c', 'elf/validate.c', 'test/elf_test.c', 'test/elf/load_test.c')) {
        $object = Join-Path $out ($source.Replace('/', '-') + '.obj')
        & $compiler @compileFlags ('/Fo' + $object) (Join-Path $bootRoot $source)
        if ($LASTEXITCODE) { throw "ASan compile: $source" }
        $objects += $object
    }
    $object = Join-Path $out 'address_ranges.obj'
    & $compiler @compileFlags ('/Fo' + $object) (Join-Path $projectRoot 'lib/memory/address_ranges.c')
    if ($LASTEXITCODE) { throw 'ASan address ranges compile' }
    $objects += $object
    $runner = @'
#include <stdio.h>
int run_load_tests(void);
int run_elf_tests(void);
int main(void) {
    int i, result;
    for (i = 0; i < 100; ++i) {
        result = run_load_tests();
        if (result) { printf("load case %d\n", result); return 1; }
    }
    result = run_elf_tests();
    if (result) { printf("ELF case %d\n", result); return 1; }
    puts("ASAN_ELF_PASS: 100 ownership/rollback repetitions and ELF validation");
    return 0;
}
'@
    [IO.File]::WriteAllText((Join-Path $out 'runner.c'), $runner, [Text.UTF8Encoding]::new($false))
    $linkArguments = @('/nologo', '/W4', '/WX', '/fsanitize=address', '/Zi', '/MD',
        ('/Tc' + $out + '/runner.c'), ('/Fo' + $out + '/runner.obj'),
        ('/Fd' + $out + '/asan.pdb'), ('/Fe' + $out + '/tests.exe')) + $objects +
        @((Join-Path $bootRoot 'build/segment_bounds.obj'), '/link', '/incremental:no')
    & $compiler @linkArguments
    if ($LASTEXITCODE) { throw 'ASan link' }
    Copy-Item ($vc.FullName + '/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out
    & (Join-Path $out 'tests.exe')
    if ($LASTEXITCODE) { throw 'ASan tests' }
    Write-BootStatus OK 'ASan ELF e accounting delle allocazioni superati.'
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
}

# ============ TEN CONSECUTIVE ELF BOOTS ============

Write-BootSection 'dieci boot ELF consecutivi'
$esp = Join-Path $out 'esp'
$bootDirectory = Join-Path $esp 'EFI/BOOT'
New-Item -ItemType Directory -Path $bootDirectory | Out-Null
Copy-Item (Join-Path $bootRoot 'build/esp/EFI/BOOT/BOOTX64.EFI') $bootDirectory
# Existing ordinary kernel is an integration fixture; no kernel source is changed.
Copy-Item (Join-Path $projectRoot 'kernel/build/kernel.elf') $bootDirectory
[IO.File]::WriteAllText((Join-Path $bootDirectory 'boot-check.txt'), "OS boot check`r`n")
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
            if ($vm.HasExited) { throw "QEMU exited: boot $index" }
            if (Test-Path $serial) {
                $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
                if ($text -match '\[ FAIL \]|KERNEL PANIC') { throw "Boot failure: $index" }
                if ($text.Contains('KERNEL_READY')) { $passed = $true; break }
            }
            Start-Sleep -Milliseconds 50
        }
        if (!$passed) { throw "Boot timeout: $index" }
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        if ($started) {
            [IO.File]::WriteAllText((Join-Path $out "stderr-$index.log"), $stderr.GetAwaiter().GetResult())
        }
        $vm.Dispose()
    }
    if ([IO.File]::ReadAllText($debug).Contains('Triple fault')) { throw "Triple fault: $index" }
    Write-BootStatus OK "Boot $index/10: KERNEL_READY, nessun errore inatteso."
}
Write-BootStatus INFO "Evidenze: $out"
