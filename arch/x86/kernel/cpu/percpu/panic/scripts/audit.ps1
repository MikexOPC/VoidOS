param([Parameter(Mandatory)][string]$ImageDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$saved = Use-Msvc
try {
    $objects = @('arch-x86-kernel-cpu-percpu-panic-records.obj','arch-x86-kernel-cpu-percpu-panic-transport-wait.obj',
        'arch-x86-kernel-cpu-percpu-panic-output-write.obj','arch-x86-kernel-cpu-percpu-panic-native-access.obj',
        'arch-x86-kernel-cpu-percpu-panic-native-dispatch.obj','panic-coordinator.obj','panic-capture.obj','sync-atomics.obj','apic-capture.obj','io.obj','pm-read.obj')
    if (Test-Path -LiteralPath (Join-Path $ImageDirectory 'ap-stress-barrier.obj')) {
        $objects += @('ap-stress-barrier.obj','ap-stress-trigger.obj')
    }
    foreach ($object in $objects) {
        $path = Join-Path $ImageDirectory $object
        $listing = & "$($saved.Bin)/dumpbin.exe" /nologo /disasm $path
        if ($LASTEXITCODE) { throw "Cannot disassemble $path" }
        $listing | Set-Content ($path + '.asm.txt')
        if ($listing -match '\b[xyz]mm[0-9]+\b|\b(fld|fstp?|fadd|fsub|fmul|fdiv|fxsave|fxrstor)\b') {
            throw "FP/SIMD dependency in emergency object $path"
        }
    }
} finally { Restore-Msvc $saved }
'PASS: emergency object code has no FP/SIMD register/instruction dependency'
