param([Parameter(Mandatory)][string]$Packages, [ValidateRange(1,20)][int]$Runs = 10,
    [string]$Python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
$out = Join-Path $project ('kernel/build/ap-blob-cpu-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ============ owned CPU fixture, no kernel/ESP installation ============

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
foreach ($mode in 0..2) {
    Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror',"-DMODE=$mode",'-o',"$out/high-$mode.bin",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/blob/assembly/handoff/entry.asm") -Log "$out/build.log"
}
$arguments = @(('"' + "$project/test/arch/x86/kernel/cpu/percpu/startup/blob/firmware.py" + '"'),
    '--packages',('"' + [IO.Path]::GetFullPath($Packages) + '"'),'--out',('"' + $out + '"'),'--runs',"$Runs")
$runner = Start-Process -FilePath $Python -ArgumentList (@('-B','-u') + $arguments) -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput "$out/runner.log" -RedirectStandardError "$out/runner-error.log"
Get-Content "$out/runner.log" | Out-Host
if ($runner.ExitCode) { throw "Blob CPU experiment failed; evidence: $out" }
"PASS: blob CPU evidence $out; AP/kernel gate remains open"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
