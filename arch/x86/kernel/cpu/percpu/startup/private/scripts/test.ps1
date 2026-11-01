param([Parameter(Mandatory)][string]$Packages, [ValidateRange(1,20)][int]$Runs = 10,
    [string]$Python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
$out = Join-Path $project ('kernel/build/ap-private-cpu-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ============ warning-clean COFF object and C ABI ============

Invoke-OsChecked $tool.Nasm @('-f','win64','-Wall','-Werror','-o',"$out/private.obj",
    "$project/arch/x86/kernel/cpu/percpu/startup/assembly/private/entry.asm") -Log "$out/build.log"
$saved = Use-Msvc
try {
    Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2',"/I$project",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/private/abi.c","/Fo$out/abi.obj","/Fe$out/abi.exe") -Log "$out/build.log"
    Invoke-OsChecked "$out/abi.exe" @() -Log "$out/build.log"
    Invoke-OsChecked "$($saved.Bin)/dumpbin.exe" @('/symbols','/relocations',"$out/private.obj") -Log "$out/coff.log"
    $undefined = @(Get-Content "$out/coff.log" | Where-Object { $_ -match '\bUNDEF\b' })
    if ($undefined.Count -ne 1 -or $undefined[0] -notmatch '\| xs_private_arrive$') { throw 'Unexpected assembly dependency' }
    Invoke-OsChecked $tool.Nasm @('-f','win64','-Wall','-Werror','-o',"$out/link.obj",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/private/assembly/fixture/link.asm") -Log "$out/build.log"
    Invoke-OsChecked "$($saved.Bin)/link.exe" @('/nologo','/WX','/nodefaultlib','/subsystem:native','/machine:x64',
        '/entry:xs_private_entry','/fixed','/base:0xffffffff80000000',"/out:$out/link-probe.exe",
        "$out/private.obj","$out/link.obj") -Log "$out/build.log"
    Invoke-Cppcheck $project $out @("$project/test/arch/x86/kernel/cpu/percpu/startup/private/abi.c")
} finally { Restore-Msvc $saved }

# ============ isolated CPU fixture, no kernel installation ============

foreach ($code in @('8000','a000')) {
    $segment = if ($code -eq '8000') {'0x800'} else {'0xa00'}
    Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror',"-DCODE_SEG=$segment",'-o',"$out/loader-$code.bin",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/blob/assembly/loader/entry.asm") -Log "$out/build.log"
    $sector = [IO.File]::ReadAllBytes("$out/loader-$code.bin")
    if ($sector.Length -ne 512 -or $sector[510] -ne 0x55 -or $sector[511] -ne 0xaa) { throw 'Invalid fixture loader' }
    $disk = New-Object byte[] 1048576
    [Array]::Copy($sector, $disk, 512)
    [IO.File]::WriteAllBytes("$out/disk-$code.img", $disk)
}
foreach ($mode in @(@{Name='positive';Value=0}, @{Name='low';Value=1}, @{Name='guard';Value=2}, @{Name='tss';Value=0})) {
    Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror',"-I$project/","-DMODE=$($mode.Value)",
        '-o',"$out/high-$($mode.Name).bin",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/private/assembly/fixture/entry.asm") -Log "$out/build.log"
}
Invoke-OsChecked $tool.Ndisasm @('-b','64',"$out/high-positive.bin") -Log "$out/decode.log"
$arguments = @(('"' + "$project/test/arch/x86/kernel/cpu/percpu/startup/private/firmware.py" + '"'),
    '--packages',('"' + [IO.Path]::GetFullPath($Packages) + '"'),'--out',('"' + $out + '"'),'--runs',"$Runs")
$runner = Start-Process -FilePath $Python -ArgumentList (@('-B','-u') + $arguments) -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput "$out/runner.log" -RedirectStandardError "$out/runner-error.log"
Get-Content "$out/runner.log" | Out-Host
if ($runner.ExitCode) { throw "Private CPU experiment failed; evidence: $out" }
"PASS: private prefix CPU evidence $out; AP/kernel gate remains open"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
