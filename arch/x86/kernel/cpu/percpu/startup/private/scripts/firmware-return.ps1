param([Parameter(Mandatory)][string]$Packages, [Parameter(Mandatory)][string]$Plan, [switch]$Bridge,
    [ValidateRange(1,20)][int]$Runs = 10,
    [string]$Python = "$env:USERPROFILE/AppData/Local/Programs/Python/Python312/python.exe")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
$out = Join-Path $project ('kernel/build/ap-return-cpu-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual production prefix and return objects ============

foreach ($part in @('entry','resume')) {
    Invoke-OsChecked $tool.Nasm @('-f','win64','-Wall','-Werror','-o',"$out/$part.obj",
        "$project/arch/x86/kernel/cpu/percpu/startup/assembly/private/$part.asm") -Log "$out/build.log"
}
$saved = Use-Msvc
try {
    $linkSource = if ($Bridge) {'test/arch/x86/kernel/cpu/percpu/admission/assembly/bridge/link.asm'} else {
        'test/arch/x86/kernel/cpu/percpu/startup/private/assembly/fixture/link.asm'
    }
    Invoke-OsChecked $tool.Nasm @('-f','win64','-Wall','-Werror','-o',"$out/link.obj",
        "$project/$linkSource") -Log "$out/build.log"
    $bridgeObjects = @()
    if ($Bridge) {
        Invoke-OsChecked $tool.Nasm @('-f','win64','-Wall','-Werror','-o',"$out/bridge.obj",
            "$project/arch/x86/kernel/cpu/percpu/admission/assembly/entry/bridge.asm") -Log "$out/build.log"
        $bridgeObjects += "$out/bridge.obj"
    }
    Invoke-OsChecked "$($saved.Bin)/link.exe" (@('/nologo','/WX','/nodefaultlib','/subsystem:native','/machine:x64',
        '/entry:xs_private_entry','/fixed','/base:0xffffffff80000000',"/out:$out/link-probe.exe",
        "$out/entry.obj","$out/resume.obj","$out/link.obj") + $bridgeObjects) -Log "$out/build.log"
    Invoke-OsChecked "$($saved.Bin)/dumpbin.exe" @('/symbols','/relocations',"$out/resume.obj") -Log "$out/coff.log"
    $undefined = @(Get-Content "$out/coff.log" | Where-Object { $_ -match '\bUNDEF\b' })
    if ($undefined.Count -ne 1 -or $undefined[0] -notmatch '\| xs_private_fault$') { throw 'Unexpected resume dependency' }
} finally { Restore-Msvc $saved }

# ============ standalone CPU fixture ============

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
foreach ($part in @(@{Name='entry';Output='high'}, @{Name='target';Output='target'})) {
    $fixture = if ($Bridge) { 'test/arch/x86/kernel/cpu/percpu/admission/assembly/bridge/' +
        $(if ($part.Name -eq 'entry') {'fixture.asm'} else {'target.asm'}) } else {
        "test/arch/x86/kernel/cpu/percpu/startup/private/return/assembly/$($part.Name)/entry.asm"
    }
    Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror',"-I$project/",'-o',"$out/$($part.Output).bin",
        "$project/$fixture") -Log "$out/build.log"
}
Invoke-OsChecked $tool.Ndisasm @('-b','64',"$out/high.bin") -Log "$out/decode.log"
$arguments = @('-B','-u',('"' + "$project/test/arch/x86/kernel/cpu/percpu/startup/private/return/firmware.py" + '"'),
    '--packages',('"' + [IO.Path]::GetFullPath($Packages) + '"'),'--plan',('"' + [IO.Path]::GetFullPath($Plan) + '"'),
    '--out',('"' + $out + '"'),'--runs',"$Runs")
$runner = Start-Process -FilePath $Python -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput "$out/runner.log" -RedirectStandardError "$out/runner-error.log"
Get-Content "$out/runner.log" | Out-Host
if ($runner.ExitCode) { throw "Private return experiment failed; evidence: $out" }
"PASS: private IRETQ CPU evidence $out; native runtime and AP gates remain open"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
