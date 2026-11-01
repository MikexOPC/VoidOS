param([ValidateRange(1,20)][int]$Runs = 10,
    [string]$Python = "$env:LOCALAPPDATA/Programs/Python/Python312/python.exe")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
$tools = & (Join-Path $PSScriptRoot 'assembler.ps1')
$out = Join-Path $project ('kernel/build/ap-nmi-experiment-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
. (Join-Path $project 'scripts/lib/common.ps1')

# ============ isolated CPU experiment image, not the kernel ============

Invoke-OsChecked $tools.Nasm @('-f','bin','-Wall','-Werror','-l',"$out/sector.lst",'-o',"$out/sector.bin",
    "$project/test/arch/x86/kernel/cpu/percpu/startup/assembly/nmi/transition.asm") -Log "$out/build.log"
$sector = [IO.File]::ReadAllBytes("$out/sector.bin")
if ($sector.Length -ne 512 -or $sector[510] -ne 0x55 -or $sector[511] -ne 0xaa) { throw 'Invalid experiment sector' }
$disk = New-Object byte[] 1048576
[Array]::Copy($sector, $disk, 512)
[IO.File]::WriteAllBytes("$out/disk.img", $disk)
$arguments = @(('"' + "$project/test/arch/x86/kernel/cpu/percpu/startup/scripts/nmi.py" + '"'),
    '--image',('"' + "$out/disk.img" + '"'),'--out',('"' + $out + '"'),'--runs',"$Runs")
$runner = Start-Process -FilePath $Python -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput "$out/runner.log" -RedirectStandardError "$out/runner-error.log"
Get-Content -LiteralPath "$out/runner.log" | Out-Host
if ($runner.ExitCode) { throw "NMI experiment failed; evidence: $out; see runner-error.log" }
"Evidence: $out"
