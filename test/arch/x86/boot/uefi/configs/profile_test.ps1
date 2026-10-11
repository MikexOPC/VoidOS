param([Parameter(Mandatory)][string]$ToolchainBin)
$ErrorActionPreference = 'Stop'
$bootRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../../arch/x86/boot/uefi')).Path
$projectRoot = (Resolve-Path (Join-Path $bootRoot '../../../..')).Path
. (Join-Path $bootRoot 'scripts/status.ps1')

# ====== copie isolate del profilo per prove di compilazione ====== #

Write-BootSection 'vincoli del profilo x86'
$output = Join-Path $bootRoot ('build\profile-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$original = [IO.File]::ReadAllText((Join-Path $projectRoot 'arch/x86/configs/boot_profile.h'))
$source = Join-Path $output 'consumer.c'
[IO.File]::WriteAllText($source, '#include "boot_profile.h"')
$cases = @(
    @{ Name='valid'; Before=''; After='' },
    @{ Name='page-size'; Before='#define X86_BOOT_PAGE_BYTES 4096ULL'; After='#define X86_BOOT_PAGE_BYTES 8192ULL' },
    @{ Name='unaligned'; Before='0x0000600000000000ULL'; After='0x0000600000000001ULL' },
    @{ Name='noncanonical'; Before='0x0000600000000000ULL'; After='0x0000800000000000ULL' },
    @{ Name='overlap'; Before='0x0000600000000000ULL'; After='0x0000700000000000ULL' },
    @{ Name='stack-overflow'; Before='0x0000700000000000ULL'; After='0x00007ffffffff000ULL' },
    @{ Name='empty-stack'; Before='#define X86_BOOT_STACK_PAGES 16ULL'; After='#define X86_BOOT_STACK_PAGES 0ULL' },
    @{ Name='stack-budget'; Before='#define X86_BOOT_STACK_PAGES 16ULL'; After='#define X86_BOOT_STACK_PAGES 17ULL' },
    @{ Name='table-budget'; Before='#define X86_BOOT_TABLE_LIMIT 128U'; After='#define X86_BOOT_TABLE_LIMIT 129U' }
)

# ====== profilo valido e rifiuto delle configurazioni incoerenti ====== #

foreach ($case in $cases) {
    $text = $original
    if ($case.Before) {
        if (!$text.Contains($case.Before)) { throw "Fixture non aggiornata: $($case.Name)" }
        $text = $text.Replace($case.Before, $case.After)
    }
    [IO.File]::WriteAllText((Join-Path $output 'boot_profile.h'), $text)
    foreach ($language in @('C', 'Cpp')) {
        $mode = if ($language -eq 'C') { @('/TC', '/std:c11') } else { @('/TP', '/std:c++17') }
        $object = Join-Path $output ($case.Name + '-' + $language + '.obj')
        $log = Join-Path $output ($case.Name + '-' + $language + '.log')
        & (Join-Path $ToolchainBin 'cl.exe') /nologo /W4 /WX /GS- /Zl /c @mode $source ("/Fo$object") *> $log
        $compiled = $LASTEXITCODE -eq 0
        if ($compiled -ne ($case.Name -eq 'valid')) {
            throw "Esito inatteso per $($case.Name) ($language); diagnostica: $log"
        }
        if (!$compiled -and !([IO.File]::ReadAllText($log).Contains('error C2338:'))) {
            throw "Il rifiuto non proviene da un assert del profilo: $log"
        }
    }
}
Write-BootStatus OK 'Profilo valido e otto configurazioni rifiutate verificati in C e C++.'
