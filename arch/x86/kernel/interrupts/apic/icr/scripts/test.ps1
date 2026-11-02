param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/apic-icr-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ============ shared ICR send, host sanitizer ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Invoke-OsChecked $Program $Arguments -Log "$out/tools.log" -Tee
}
$sources = @('arch/x86/kernel/interrupts/apic/icr/icr.c','test/arch/x86/kernel/interrupts/apic/icr/icr_test.c')
$saved = Use-Msvc
try {
    $objects = @()
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/O2',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source",
            "/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
"PASS: shared ICR host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
