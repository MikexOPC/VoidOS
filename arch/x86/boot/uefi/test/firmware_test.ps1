param(
    [string]$QemuDirectory = 'C:\Program Files\qemu',
    [ValidateRange(5, 120)][int]$TimeoutSeconds = 35
)
$ErrorActionPreference = 'Stop'

# ====== controllo strumenti e immagine ====== #

$bootRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $bootRoot 'scripts\status.ps1')
. (Join-Path $bootRoot 'test\fixtures\elf_fixture.ps1')
Write-BootSection 'test firmware UEFI'
$image = Join-Path $bootRoot 'build\esp\EFI\BOOT\BOOTX64.EFI'
$qemu = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$code = Join-Path $QemuDirectory 'share\edk2-x86_64-code.fd'
$template = Join-Path $QemuDirectory 'share\edk2-i386-vars.fd'
foreach ($required in @($image, $qemu, $code, $template)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "File mancante: $required"
    }
}
$testRoot = Join-Path $bootRoot ('build\firmware-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$imageHash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
Write-BootStatus INFO "EFI SHA256: $imageHash"

# ====== scenari con volumi separati ====== #

foreach ($scenario in @('valid', 'kernel-probe', 'missing', 'altered', 'kernel-missing', 'kernel-truncated',
                        'kernel-oversized', 'kernel-bad-segment', 'kernel-bad-magic', 'cpu-no-nx')) {
    $validCase = $scenario -in @('valid', 'kernel-probe')
    $scenarioRoot = Join-Path $testRoot $scenario
    $esp = Join-Path $scenarioRoot 'esp'
    $bootDirectory = Join-Path $esp 'EFI\BOOT'
    New-Item -ItemType Directory -Path $bootDirectory | Out-Null
    $scenarioImage = Join-Path $bootDirectory 'BOOTX64.EFI'
    Copy-Item -LiteralPath $image -Destination $scenarioImage
    if ((Get-FileHash -LiteralPath $scenarioImage -Algorithm SHA256).Hash -ne $imageHash) {
        throw "Immagine EFI cambiata prima dello scenario $scenario"
    }
    $variables = Join-Path $scenarioRoot 'vars.fd'
    Copy-Item -LiteralPath $template -Destination $variables
    if ($scenario -ne 'missing') {
        $contents = if ($scenario -eq 'altered') { "XS boot check`r`n" } else { "OS boot check`r`n" }
        [IO.File]::WriteAllText((Join-Path $bootDirectory 'boot-check.txt'), $contents, [Text.Encoding]::ASCII)
    }
    $kernelPath = Join-Path $bootDirectory 'kernel.elf'
    if ($scenario -ne 'kernel-missing') {
        $kernelBytes = if ($scenario -eq 'kernel-probe') {
            [IO.File]::ReadAllBytes((Join-Path $bootRoot 'build\kernel_probe.elf'))
        } else { New-KernelElfFixture }
        if ($scenario -eq 'kernel-truncated') { $kernelBytes = [byte[]]$kernelBytes[0..127] }
        if ($scenario -eq 'kernel-bad-segment') {
            [BitConverter]::GetBytes([uint64]1).CopyTo($kernelBytes, 104)
        }
        if ($scenario -eq 'kernel-bad-magic') { $kernelBytes[0] = 0 }
        [IO.File]::WriteAllBytes($kernelPath, $kernelBytes)
        if ($scenario -eq 'kernel-oversized') {
            $largeFile = [IO.File]::Open($kernelPath, [IO.FileMode]::Open, [IO.FileAccess]::Write)
            try { $largeFile.SetLength(64MB + 1) } finally { $largeFile.Dispose() }
        }
    }
    $serial = Join-Path $scenarioRoot 'serial.log'
    $stderr = Join-Path $scenarioRoot 'stderr.log'
    $probeFailure = $scenario -eq 'missing' -or $scenario -eq 'altered'
    $expected = if ($validCase) {
        '[  OK  ] ELF64 verificato; buffer e file rilasciati.'
    } elseif ($scenario -eq 'cpu-no-nx') {
        '[ FAIL ] CPU non compatibile con il profilo paging richiesto.'
    } elseif ($probeFailure) {
        '[ FAIL ] verifica file fallita.'
    } else {
        '[ FAIL ] validazione kernel fallita.'
    }
    $unexpected = if ($validCase) { '[ FAIL ]' } elseif ($scenario -eq 'cpu-no-nx') {
        '[  OK  ] Long mode, NX e larghezze degli indirizzi verificati via CPUID.'
    } else { '[  OK  ] ELF64 verificato;' }

    # ============ avvio macchina virtuale senza finestra ============ #

    $arguments = @(
        '-machine', 'q35', '-accel', 'tcg', '-m', '128', '-net', 'none',
        '-display', 'none', '-monitor', 'none', '-serial', ('file:' + $serial.Replace('\', '/')),
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + $code.Replace('\', '/')),
        '-drive', ('if=pflash,format=raw,file=' + $variables.Replace('\', '/')),
        '-drive', ('format=raw,file=fat:rw:' + $esp.Replace('\', '/'))
    )
    if ($scenario -eq 'cpu-no-nx') { $arguments += @('-cpu', 'qemu64,-nx') }
    $quotedArguments = $arguments | ForEach-Object { '"' + $_ + '"' }
    $vm = $null
    $passed = $false
    try {
        $vm = Start-Process -FilePath $qemu -ArgumentList $quotedArguments -WindowStyle Hidden `
            -PassThru -RedirectStandardError $stderr
        $timer = [Diagnostics.Stopwatch]::StartNew()

        # ============ verifica messaggi e ritorno al firmware ============ #

        while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
            $log = ''
            if (Test-Path -LiteralPath $serial) {
                $stream = [IO.File]::Open($serial, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $reader = [IO.StreamReader]::new($stream)
                try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
            }
            $log = [regex]::Replace($log, '\x1b\[[0-?]*[ -/]*[@-~]', '')
            if ($log.Contains($unexpected)) { throw "Esito inatteso nello scenario $scenario" }
            $sectionIndex = $log.IndexOf("# ============ avvio UEFI ============ #`r`n`r`n")
            $startIndex = $log.IndexOf('[  OK  ] avvio UEFI riuscito.')
            $fileSectionIndex = $log.IndexOf("# ============ verifica file di avvio ============ #`r`n`r`n")
            $infoIndex = $log.IndexOf('[ INFO ] Lettura di EFI/BOOT/boot-check.txt')
            $resultIndex = $log.IndexOf($expected)
            $kernelSectionIndex = $log.IndexOf("# ============ validazione kernel ============ #`r`n`r`n")
            $kernelInfoIndex = $log.IndexOf('[ INFO ] Lettura e controllo di EFI/BOOT/kernel.elf')
            $kernelSequence = $probeFailure -or
                ($kernelSectionIndex -gt $infoIndex -and $kernelInfoIndex -gt $kernelSectionIndex -and
                 $resultIndex -gt $kernelInfoIndex)
            if ($validCase) {
                $loadSection = $log.IndexOf("# ============ preparazione segmenti ELF ============ #`r`n`r`n")
                $loadSuccess = $log.IndexOf('[  OK  ] Segmenti copiati e BSS azzerata; pagine conservate.')
                $kernelSequence = $kernelSequence -and $loadSection -gt $kernelInfoIndex -and
                    $loadSuccess -gt $loadSection -and $resultIndex -gt $loadSuccess
            }
            $returnPattern = if ($validCase) {
                'BdsDxe: starting Boot[0-9A-Fa-f]+ "UiApp"'
            } elseif ($scenario -eq 'cpu-no-nx') {
                'BdsDxe: failed to start[^\r\n]*: Unsupported'
            } elseif ($scenario -eq 'missing' -or $scenario -eq 'kernel-missing') {
                'BdsDxe: failed to start[^\r\n]*: Not Found'
            } else {
                'BdsDxe: failed to start[^\r\n]*: Load Error'
            }
            $returnMatch = [regex]::Match($log, $returnPattern)
            $mapSequence = $true
            if ($validCase) {
                $mapSection = $log.IndexOf("# ============ mappa memoria UEFI ============ #`r`n`r`n")
                $mapSuccess = $log.IndexOf('[  OK  ] Mappa memoria verificata; buffer temporaneo rilasciato.')
                $regionSuccess = $log.IndexOf('[  OK  ] Regioni classificate; allocazioni ancora gestite da UEFI.')
                $releaseSuccess = $log.IndexOf('[  OK  ] Pagine kernel rilasciate prima del ritorno al firmware.')
                $stackSection = $log.IndexOf("# ============ stack iniziale kernel ============ #`r`n`r`n")
                $stackSuccess = $log.IndexOf('[  OK  ] Stack dedicato di 64 KiB allocato e azzerato.')
                $coverageSuccess = $log.IndexOf('[  OK  ] Kernel e stack coperti dalla mappa come EfiLoaderData.')
                $planSection = $log.IndexOf("# ============ piano mappature kernel ============ #`r`n`r`n")
                $planSuccess = $log.IndexOf('[  OK  ] Piano verificato; pagina di guardia riservata, tabelle ancora inattive.')
                $cpuSection = $log.IndexOf("# ============ capacita paging CPU ============ #`r`n`r`n")
                $cpuSuccess = $log.IndexOf('[  OK  ] Long mode, NX e larghezze degli indirizzi verificati via CPUID.')
                $tablesSection = $log.IndexOf("# ============ costruzione tabelle di pagina ============ #`r`n`r`n")
                $tablesSuccess = $log.IndexOf('[  OK  ] Tabelle costruite e coperte da EfiLoaderData; CR3 invariato.')
                $contractSection = $log.IndexOf("# ============ contratto ingresso kernel ============ #`r`n`r`n")
                $contractSuccess = $log.IndexOf('[  OK  ] Contratto su pagina R/NX e stack ABI verificati; tabelle inattive.')
                $finalMapSection = $log.IndexOf("# ============ mappa persistente per uscita UEFI ============ #`r`n`r`n")
                $finalMapSuccess = $log.IndexOf('[  OK  ] Mappa persistente R/NX e contratto v2 verificati; tutte le risorse coperte.')
                $exitPreflight = $log.IndexOf('[  OK  ] Preflight uscita UEFI verificato; servizi firmware ancora attivi.')
                $gdtSection = $log.IndexOf("# ============ descrittori segmenti x64 ============ #`r`n`r`n")
                $gdtSuccess = $log.IndexOf('[  OK  ] GDT ring 0 e operando GDTR verificati; descrittori ancora inattivi.')
                $idtSection = $log.IndexOf("# ============ descrittori eccezioni x64 ============ #`r`n`r`n")
                $idtSuccess = $log.IndexOf('[  OK  ] IDT, IDTR e 32 ingressi terminali verificati; eccezioni ancora gestite dal firmware.')
                $persistentSection = $log.IndexOf("# ============ descrittori e stack persistenti ============ #`r`n`r`n")
                $persistentSuccess = $log.IndexOf('[  OK  ] Descrittori e due stack RW/NX verificati; guard page assenti, CR3 invariato e preflight aggiornato.')
                $pagingSection = $log.IndexOf("# ============ stato paging e CR3 firmware ============ #`r`n`r`n")
                $pagingSuccess = $log.IndexOf('[  OK  ] Paging attivo a quattro livelli verificato; CR3 letto senza modifiche.')
                $cr3Before = [regex]::Match($log, '\[ INFO \] CR3 firmware: 0x([0-9A-F]{16})')
                $cr3After = [regex]::Match($log, '\[ INFO \] CR3 firmware dopo: 0x([0-9A-F]{16})')
                $inactiveRoot = [regex]::Match($log, '\[ INFO \] Root delle tabelle inattive: 0x([0-9A-F]{13}000)')
                $registerSequence = $cr3Before.Success -and $cr3After.Success -and $inactiveRoot.Success -and
                    $cr3Before.Groups[1].Value -eq $cr3After.Groups[1].Value -and
                    $cr3Before.Index -gt $pagingSection -and $pagingSuccess -gt $cr3Before.Index -and
                    $cr3After.Index -gt $tablesSection -and $inactiveRoot.Index -gt $cr3After.Index -and
                    $tablesSuccess -gt $inactiveRoot.Index
                if ($registerSequence) {
                    $activeRoot = [Convert]::ToUInt64($cr3Before.Groups[1].Value, 16) -band [uint64]0x000ffffffffff000
                    $registerSequence = $activeRoot -ne [Convert]::ToUInt64($inactiveRoot.Groups[1].Value, 16)
                }
                $mapSequence = $stackSection -gt $resultIndex -and $stackSuccess -gt $stackSection -and $mapSection -gt $stackSuccess -and $mapSuccess -gt $mapSection -and
                    $regionSuccess -gt $mapSuccess -and
                    $returnMatch.Success -and $coverageSuccess -gt $regionSuccess -and $planSection -gt $coverageSuccess -and
                    $planSuccess -gt $planSection -and $cpuSection -gt $planSuccess -and $cpuSuccess -gt $cpuSection -and
                    $pagingSection -gt $cpuSuccess -and $tablesSection -gt $pagingSuccess -and $registerSequence -and
                    $contractSection -gt $tablesSuccess -and $contractSuccess -gt $contractSection -and
                    $finalMapSection -gt $contractSuccess -and $finalMapSuccess -gt $finalMapSection -and
                    $exitPreflight -gt $finalMapSuccess -and $gdtSection -gt $exitPreflight -and
                    $gdtSuccess -gt $gdtSection -and $idtSection -gt $gdtSuccess -and
                    $idtSuccess -gt $idtSection -and $persistentSection -gt $idtSuccess -and
                    $persistentSuccess -gt $persistentSection -and $releaseSuccess -gt $persistentSuccess -and
                    $returnMatch.Index -gt $releaseSuccess
            }
            if ($scenario -eq 'cpu-no-nx') {
                $cpuSection = $log.IndexOf("# ============ capacita paging CPU ============ #`r`n`r`n")
                $elfSuccess = $log.IndexOf('[  OK  ] ELF64 verificato; buffer e file rilasciati.')
                $releaseSuccess = $log.IndexOf('[  OK  ] Pagine kernel rilasciate prima del ritorno al firmware.')
                $mapSequence = $elfSuccess -gt $kernelInfoIndex -and $cpuSection -gt $elfSuccess -and
                    $resultIndex -gt $cpuSection -and $releaseSuccess -gt $resultIndex -and $returnMatch.Index -gt $releaseSuccess
            }
            if ($sectionIndex -ge 0 -and $startIndex -gt $sectionIndex -and
                $fileSectionIndex -gt $startIndex -and $infoIndex -gt $fileSectionIndex -and
                $resultIndex -gt $infoIndex -and
                $kernelSequence -and $mapSequence -and
                $returnMatch.Success -and $returnMatch.Index -gt $resultIndex) {
                $passed = $true
                break
            }
            if ($vm.WaitForExit(250)) { throw "QEMU terminato: $([IO.File]::ReadAllText($stderr))" }
        }
        if (!$passed) { throw "Timeout nello scenario $scenario. Log: $serial" }
        Write-BootStatus OK "$scenario`: separatori, livelli e ritorno al firmware verificati."
    } catch {
        Write-BootStatus FAIL $_.Exception.Message
        throw
    } finally {
        if ($vm) {
            if (!$vm.HasExited) {
                Stop-Process -Id $vm.Id
                $vm.WaitForExit()
            }
            $vm.Dispose()
        }
    }
}
if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $imageHash) {
    throw 'Immagine EFI cambiata durante i test firmware'
}
Write-BootStatus OK '10/10 scenari firmware verificati sulla stessa immagine EFI.'
Write-BootStatus INFO "Log: $testRoot"

