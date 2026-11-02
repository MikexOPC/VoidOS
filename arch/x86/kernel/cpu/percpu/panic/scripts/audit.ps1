param([Parameter(Mandatory)][string]$ImageDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$saved = Use-Msvc
try {
    $objects = @('arch-x86-kernel-cpu-percpu-panic-records.obj','arch-x86-kernel-cpu-percpu-panic-transport-wait.obj',
        'arch-x86-kernel-cpu-percpu-panic-output-write.obj','arch-x86-kernel-cpu-percpu-panic-native-access.obj',
        'arch-x86-kernel-cpu-percpu-panic-native-dispatch.obj','arch-x86-kernel-interrupts-apic-icr-icr.obj',
        'panic-coordinator.obj','panic-capture.obj','sync-atomics.obj','apic-capture.obj','io.obj','pm-read.obj')
    if (Test-Path -LiteralPath (Join-Path $ImageDirectory 'ap-stress-barrier.obj')) {
        $objects += @('ap-stress-barrier.obj','ap-stress-trigger.obj')
    }
    foreach ($object in $objects) {
        $path = Join-Path $ImageDirectory $object
        $listing = & "$($saved.Bin)/dumpbin.exe" /nologo /disasm $path
        if ($LASTEXITCODE) { throw "Cannot disassemble $path" }
        $listing | Set-Content ($path + '.asm.txt')
        $instructions = @([regex]::Matches(($listing -join "`n"),
            '(?im)^\s*[0-9a-f]{8,16}:\s+(?:[0-9a-f]{2}\s+)+([a-z][a-z0-9]*)([^\r\n]*)'))
        if (!$instructions.Count) { throw "Cannot audit instructions in $path" }
        foreach ($instruction in $instructions) {
            $opcode = $instruction.Groups[1].Value
            $operands = $instruction.Groups[2].Value
            if ($opcode -match '^(f[a-z0-9]*|wait|emms|vzero(all|upper)|v?(ldmxcsr|stmxcsr)|xsave[a-z0-9]*|xrstor[a-z0-9]*)$' -or
                $operands -match '\b([xyz]mm[0-9]+|mm[0-7]|k[0-7]|tmm[0-7])\b') {
                throw "FP/SIMD dependency in emergency object $path"
            }
        }
    }
} finally { Restore-Msvc $saved }
'PASS: emergency object code has no FP/SIMD register/instruction dependency'
