param([string]$QemuDirectory = 'C:/Program Files/qemu', [switch]$UseInstalled, [string]$ImageDirectory)
. (Join-Path $PSScriptRoot '../../scripts/lib/layout.ps1')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
. (Join-Path $PSScriptRoot 'firmware/acpi/scripts/firmware.ps1')
. (Join-Path $PSScriptRoot 'interrupts/apic/scripts/firmware.ps1')
. (Join-Path $PSScriptRoot 'serial/scripts/firmware.ps1')
. (Join-Path $PSScriptRoot 'interrupts/registry/scripts/firmware.ps1')
. (Join-Path $PSScriptRoot 'timer/lapic/scripts/firmware.ps1')
$output = Join-Path $project ('kernel/build/firmware-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
$image = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
if ($ImageDirectory) {
    if ($UseInstalled) { throw 'Choose installed or isolated images.' }
    foreach ($variant in @('ready','panic','execute','unmapped','guard-low','guard-high','opcode','selector',
        'divide','stack','nested','double','main-retired','main-guard-high','main-execute','main-return',
        'region-retired','region-guard-low','region-guard-high','region-execute')) {
        New-Item -ItemType Directory -Path (Join-Path $output $variant) | Out-Null
        Copy-Item -LiteralPath (Join-Path $ImageDirectory "$variant/kernel.elf") -Destination (Join-Path $output "$variant/kernel.elf")
    }
} else {
if ($UseInstalled) {
    New-Item -ItemType Directory -Path (Join-Path $output 'ready') | Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/kernel.elf') `
        -Destination (Join-Path $output 'ready/kernel.elf')
} else {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -OutputDirectory (Join-Path $output 'ready')
}
& (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -TestPanic -OutputDirectory (Join-Path $output 'panic')
foreach ($fault in @('execute','unmapped','guard-low','guard-high')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -TestWindowFault $fault -OutputDirectory (Join-Path $output $fault)
}
foreach ($fault in @('opcode','selector','divide','stack','nested','double')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -TestCpuFault $fault -OutputDirectory (Join-Path $output $fault)
}
foreach ($fault in @('retired','guard-high','execute','return')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -TestMainFault $fault -OutputDirectory (Join-Path $output ('main-' + $fault))
}
foreach ($fault in @('retired','guard-low','guard-high','execute')) {
    & (Join-Path $project 'kernel/scripts/build.ps1') -TestBootstrapMemory -TestRegionFault $fault -OutputDirectory (Join-Path $output ('region-' + $fault))
}
}
Write-BootSection 'kernel autonomo dopo uscita UEFI'
$candidatePages = @{}

# ====== campi numerici obbligatori senza valori impliciti ===== #

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text, $Pattern)
    if (!$match.Success) { throw "Campo assente: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value,16)
}

# ====== due quantita di RAM panic e fault NX oppure dopo unmap ====== #

foreach ($case in @(@{Name='ready-128';Variant='ready';Ram=128}, @{Name='ready-256';Variant='ready';Ram=256},
    @{Name='ready-smp4';Variant='ready';Ram=256;Smp=4},
    @{Name='panic-128';Variant='panic';Ram=128}, @{Name='execute-128';Variant='execute';Ram=128},
    @{Name='unmapped-128';Variant='unmapped';Ram=128}, @{Name='guard-low-128';Variant='guard-low';Ram=128},
    @{Name='guard-high-128';Variant='guard-high';Ram=128}, @{Name='opcode-128';Variant='opcode';Ram=128},
    @{Name='selector-128';Variant='selector';Ram=128}, @{Name='divide-128';Variant='divide';Ram=128},
    @{Name='stack-128';Variant='stack';Ram=128}, @{Name='nested-128';Variant='nested';Ram=128},
    @{Name='double-128';Variant='double';Ram=128}, @{Name='nmi-128';Variant='ready';Ram=128;Nmi=$true},
    @{Name='main-retired-128';Variant='main-retired';Ram=128}, @{Name='main-guard-high-128';Variant='main-guard-high';Ram=128},
    @{Name='main-execute-128';Variant='main-execute';Ram=128}, @{Name='main-return-128';Variant='main-return';Ram=128},
    @{Name='region-retired-128';Variant='region-retired';Ram=128}, @{Name='region-guard-low-128';Variant='region-guard-low';Ram=128},
    @{Name='region-guard-high-128';Variant='region-guard-high';Ram=128}, @{Name='region-execute-128';Variant='region-execute';Ram=128})) {
    $scenario = Join-Path $output $case.Name
    $boot = Join-Path $scenario 'esp/EFI/BOOT'
    New-Item -ItemType Directory -Force -Path $boot | Out-Null
    Copy-Item -LiteralPath $image -Destination (Join-Path $boot 'BOOTX64.EFI')
    Copy-Item -LiteralPath (Join-Path $output ($case.Variant + '/kernel.elf')) -Destination (Join-Path $boot 'kernel.elf')
    [IO.File]::WriteAllText((Join-Path $boot 'boot-check.txt'), "OS boot check`r`n", [Text.Encoding]::ASCII)
    $vars = Join-Path $scenario 'vars.fd'
    Copy-Item -LiteralPath (Join-Path $QemuDirectory 'share/edk2-i386-vars.fd') -Destination $vars
    $serial = Join-Path $scenario 'serial.log'
    $cpuLog = Join-Path $scenario 'qemu.log'
    $arguments = @('-machine','q35','-cpu','max','-accel','tcg','-m',[string]$case.Ram,'-net','none',
        '-smp',$(if ($case.Smp) {[string]$case.Smp} else {'1'}),
        '-d','int,cpu_reset','-D',$cpuLog.Replace('\','/'),
        '-display','none','-monitor','stdio','-no-reboot','-serial',('file:' + $serial.Replace('\','/')),
        '-drive',('if=pflash,format=raw,readonly=on,file=' + (Join-Path $QemuDirectory 'share/edk2-x86_64-code.fd').Replace('\','/')),
        '-drive',('if=pflash,format=raw,file=' + $vars.Replace('\','/')),
        '-drive',('format=raw,file=fat:rw:' + (Join-Path $scenario 'esp').Replace('\','/')))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
    $start.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $vm = [Diagnostics.Process]::new(); $vm.StartInfo = $start
    $started = $false
    try {
        $started = $vm.Start()
        if (!$started) { throw 'QEMU non avviato.' }
        $stdout = $vm.StandardOutput.ReadToEndAsync(); $stderr = $vm.StandardError.ReadToEndAsync()
        $faultCase = $case.Nmi -or $case.Variant.StartsWith('region-') -or $case.Variant -in @('execute','unmapped','guard-low','guard-high','opcode','selector','divide','stack','double','main-retired','main-guard-high','main-execute')
        $nestedCase = $case.Variant -eq 'nested'
        $expected = if ($nestedCase) { 'KERNEL_NESTED_FAULT_READY' } elseif ($faultCase) { 'KERNEL_EXCEPTION_HALTED' } elseif ($case.Variant -eq 'panic') { 'INTENTIONAL_TEST_PANIC' } elseif ($case.Variant -eq 'main-return') { 'KERNEL_MAIN_RETURN_TEST_READY' } else { 'KERNEL_READY' }
        $deadline = [DateTime]::UtcNow.AddSeconds(40)
        $found = $false
        $nmiInjected = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $serial) {
                $stream = [IO.File]::Open($serial, 'Open', 'Read', 'ReadWrite')
                $reader = [IO.StreamReader]::new($stream)
                try { $raw = $reader.ReadToEnd() } finally { $reader.Dispose() }
                $log = [regex]::Replace($raw, '\x1b\[[0-?]*[ -/]*[@-~]', '')
                if ($case.Nmi -and !$nmiInjected -and $log.Contains('KERNEL_READY')) {
                    $vm.StandardInput.WriteLine('nmi'); $vm.StandardInput.Flush()
                    $nmiInjected = $true
                }
                if ($log.Contains($expected)) { $found = $true; break }
            }
            if ($vm.HasExited) { throw 'QEMU terminato prima del kernel.' }
            Start-Sleep -Milliseconds 100
        }
        if (!$found) { throw "Messaggio kernel assente: $scenario" }
        Start-Sleep -Milliseconds 300
        $exit = [regex]::Match($log, 'ACTIVATION_EXIT_STATE: 0x([0-9A-F]{16})')
        if (!$exit.Success) { throw 'Indirizzo dello stato UEFI assente.' }
        $vm.StandardInput.WriteLine('stop'); $vm.StandardInput.WriteLine('info registers')
        $istPte = [regex]::Match($log, '(?i)KERNEL_IST_PTE: 0x([0-9a-f]{16})')
        if (!$istPte.Success) { throw 'Vista PTE stack kernel assente.' }
        $vm.StandardInput.WriteLine('x /16gx 0x' + $istPte.Groups[1].Value)
        $windowPte = [Convert]::ToUInt64($istPte.Groups[1].Value,16) - 31 * 8
        $vm.StandardInput.WriteLine('x /16gx 0x' + $windowPte.ToString('x16'))
        $mainPte = [regex]::Match($log, '(?i)KERNEL_MAIN_PTE: 0x([0-9a-f]{16})')
        $oldPte = [regex]::Match($log, '(?i)KERNEL_OLD_STACK_PTE: 0x([0-9a-f]{16})')
        if (!$mainPte.Success -or !$oldPte.Success) { throw 'PTE stack principale assenti.' }
        $vm.StandardInput.WriteLine('x /18gx 0x' + $mainPte.Groups[1].Value)
        $vm.StandardInput.WriteLine('x /18gx 0x' + $oldPte.Groups[1].Value)
        $regionPte = Read-Hex $log 'KERNEL_REGION_PTE: 0x([0-9a-f]{16})'
        $vm.StandardInput.WriteLine('x /136gx 0x' + $regionPte.ToString('x16'))
        $arenaContext = Read-Hex $log 'KERNEL_ARENA_CONTEXT: 0x([0-9a-f]{16})'
        $arenaInventory = Read-Hex $log 'KERNEL_ARENA_INVENTORY: 0x([0-9a-f]{16})'
        $vm.StandardInput.WriteLine('x /21gx 0x' + $arenaContext.ToString('x16'))
        $vm.StandardInput.WriteLine('x /7gx 0x' + ($arenaContext + 112).ToString('x16'))
        $vm.StandardInput.WriteLine('x /3gx 0x' + $arenaInventory.ToString('x16'))
        $pagingInventory = Read-Hex $log 'KERNEL_PAGING_INVENTORY: 0x([0-9a-f]{16})'
        $vm.StandardInput.WriteLine('x /3gx 0x' + $pagingInventory.ToString('x16'))
        $acpiDirectory = Read-Hex $log 'KERNEL_ACPI_DIRECTORY: 0x([0-9a-f]{16})'
        $vm.StandardInput.WriteLine('x /3gx 0x' + $acpiDirectory.ToString('x16'))
        Write-MadtMonitor $log $vm
        Write-ApicMonitor $log $vm
        Write-UartMonitor $log $vm
        $tss = [regex]::Match($log, '(?i)KERNEL_TSS_BASE: 0x([0-9a-f]{16})')
        if (!$tss.Success) { throw 'TSS kernel assente.' }
        $vm.StandardInput.WriteLine('x /1gx 0x' + ([Convert]::ToUInt64($tss.Groups[1].Value,16) + 4).ToString('x16'))
        $gdt = [regex]::Match($log, '(?i)KERNEL_GDT_BASE: 0x([0-9a-f]{16})')
        if (!$gdt.Success) { throw 'GDT kernel assente.' }
        $vm.StandardInput.WriteLine('x /2gx 0x' + ([Convert]::ToUInt64($gdt.Groups[1].Value,16) + 24).ToString('x16'))
        $vm.StandardInput.WriteLine($(if ($nestedCase) { 'x /7gx $rsp' } else { 'x /8gx $rsp' }))
        if ($faultCase) {
            $frame = [regex]::Match($log, '(?i)FAULT_FRAME: 0x([0-9a-f]{16})')
            if (!$frame.Success) { throw 'Frame eccezione assente.' }
            $vm.StandardInput.WriteLine('x /7gx 0x' + $frame.Groups[1].Value)
        }
        $vm.StandardInput.WriteLine('xp /2gx 0x' + $exit.Groups[1].Value)
        $vm.StandardInput.WriteLine('quit'); $vm.StandardInput.Flush()
        if (!$vm.WaitForExit(5000)) { throw 'Monitor non risponde.' }
        $registers = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $scenario 'registers.log'), $registers)
        [IO.File]::WriteAllText((Join-Path $scenario 'stderr.log'), $stderr.GetAwaiter().GetResult())
        if (!(Test-Path -LiteralPath $cpuLog) -or (Get-Content -LiteralPath $cpuLog -Raw) -match '(?i)triple fault') {
            throw "Trace CPU assente o triple fault: $scenario"
        }
        $root = [regex]::Match($log, 'ACTIVATION_ROOT: 0x([0-9A-F]{16})').Groups[1].Value
        $marker = if ($case.Variant -eq 'panic') { '4b50414e49430001' } elseif ($case.Variant -eq 'main-return') { '4b53545245540001' } else { '4b52454144590001' }
        $exitPattern = '(?im)^0*' + $exit.Groups[1].Value.TrimStart('0') + ':\s+0x0000000[1-3]00000002\s+0x0000000000000000'
        if ($registers -notmatch $exitPattern -or
            $registers -notmatch ('(?i)CR3=' + $root) -or $registers -notmatch 'HLT=1' -or
            $registers -notmatch 'CS =0008' -or
            !$log.Contains('KERNEL_CONTRACT_OK') -or !$log.Contains('KERNEL_MEMORY_OK') -or
            !$log.Contains('KERNEL_PHYSICAL_OK') -or !$log.Contains('KERNEL_MAPPING_OK') -or
            !$log.Contains('KERNEL_PHYSICAL_ALIGNED_OK') -or
            !$log.Contains('KERNEL_REGIONS_OK') -or
            !$log.Contains('KERNEL_ARENA_OK') -or !$log.Contains('KERNEL_PERSISTENT_STATE_OK') -or
            !$log.Contains('KERNEL_PAGING_INVENTORY_OK') -or
            !$log.Contains('KERNEL_WINDOW_16_PAGES_OK') -or !$log.Contains('KERNEL_OBJECTS_OK') -or
            !$log.Contains('KERNEL_IDT_OK') -or !$log.Contains('KERNEL_DESCRIPTORS_OK') -or
            !$log.Contains('KERNEL_MAIN_STACK_OK') -or !$log.Contains('KERNEL_RSP0_OK')) {
            throw "Stato CPU o inizializzazione kernel errati: $scenario"
        }
        $aligned64 = Read-Hex $log 'KERNEL_PHYSICAL_64K_BASE: 0x([0-9a-f]{16})'
        $aligned2m = Read-Hex $log 'KERNEL_PHYSICAL_2M_BASE: 0x([0-9a-f]{16})'
        if (!$aligned64 -or !$aligned2m -or ($aligned64 -band 65535) -or ($aligned2m -band 2097151) -or
            ($aligned64 -lt $aligned2m + 32768 -and $aligned2m -lt $aligned64 + 16384)) {
            throw 'Allocazioni fisiche non allineate o sovrapposte.'
        }
        $region0 = Read-Hex $log 'KERNEL_REGION0_PHYSICAL: 0x([0-9a-f]{16})'
        $region1 = Read-Hex $log 'KERNEL_REGION1_PHYSICAL: 0x([0-9a-f]{16})'
        $pagingCount = Read-Hex $log 'KERNEL_PAGING_TABLES: 0x([0-9a-f]{16})'
        $pagingLeaves = Read-Hex $log 'KERNEL_PAGING_MIXED_LEAVES: 0x([0-9a-f]{16})'
        $pagingRoot = [Convert]::ToUInt64($root,16)
        # The framebuffer mapping is published after the inventory log line: its leaves and tables
        # belong to the final snapshot dumped at the halt.
        if ($log.Contains('KERNEL_FB_MAPPING_LIVE') -and
            (Read-Hex $log 'KERNEL_FB_MAPPING_INVENTORY: 0x([0-9a-f]{16})') -eq $pagingInventory) {
            $pagingLeaves += Read-Hex $log 'KERNEL_FB_PLAN_PAGES: 0x([0-9a-f]{16})'
            $pagingCount += Read-Hex $log 'KERNEL_FB_TABLE_COUNT: 0x([0-9a-f]{16})'
        }
        # G3: the kernel heap (slot 230) is also bound after the inventory log line.
        if ($log.Contains('KERNEL_HEAP_READY') -and
            (Read-Hex $log 'KERNEL_HEAP_INVENTORY: 0x([0-9a-f]{16})') -eq $pagingInventory) {
            $pagingLeaves += Read-Hex $log 'KERNEL_HEAP_PAGES: 0x([0-9a-f]{16})'
            $pagingCount += Read-Hex $log 'KERNEL_HEAP_TABLES: 0x([0-9a-f]{16})'
        }
        if (!$pagingCount -or $pagingCount -gt 128 -or !$pagingLeaves -or ($pagingInventory -band 7) -or
            $pagingInventory -lt $arenaInventory + 24 -or $pagingInventory + 3144 -gt 0x7000000b3000 -or
            $registers -notmatch ('(?im)^' + $pagingInventory.ToString('x16') + ':\s+0x' + $pagingRoot.ToString('x16') + '\s+0x' + $pagingLeaves.ToString('x16')) -or
            $registers -notmatch ('(?im)^' + ($pagingInventory + 16).ToString('x16') + ':\s+0x' + ((1L -shl 32) + $pagingCount).ToString('x16'))) {
            throw 'Inventario paging fuori arena o diverso dalla fotografia verificata.'
        }
        # The contract grows with its version: size and version come from the log (v3 was 0xa8 bytes),
        # the inventory follows the 16-byte aligned context.
        $contractVersion = Read-Hex $log 'KERNEL_BOOT_VERSION: 0x([0-9a-f]{16})'
        $contractSize = Read-Hex $log 'KERNEL_BOOT_SIZE: 0x([0-9a-f]{16})'
        $contextBytes = [uint64]([math]::Floor(($contractSize + 15) / 16) * 16)
        if (!$contractVersion -or !$contractSize -or ($arenaContext -band 15) -or ($arenaInventory -band 7) -or
            $arenaContext -lt 0x7000000a3000 -or $arenaContext + $contextBytes -gt 0x7000000b3000 -or
            $arenaInventory -lt 0x7000000a3000 -or $arenaInventory + 24 -gt 0x7000000b3000 -or
            ($arenaContext -lt $arenaInventory + 24 -and $arenaInventory -lt $arenaContext + $contextBytes) -or
            (Read-Hex $log 'KERNEL_ARENA_USED: 0x([0-9a-f]{16})') -ne $contextBytes + 24 -or
            $registers -notmatch ('(?im)^' + $arenaContext.ToString('x16') + ':\s+0x314f464e49545342\s+0x' +
                (($contractSize -shl 32) + $contractVersion).ToString('x16'))) {
            throw 'Contesto persistente fuori arena, sovrapposto o contratto corrotto.'
        }
        Test-AcpiFirmware $log $registers $arenaContext $pagingInventory $case $windowPte
        Test-ApicFirmware $log $registers
        Test-UartFirmware $log $registers $cpuLog
        if (!$region0 -or !$region1 -or ($region0 -band 65535) -or ($region1 -band 65535) -or
            ($region0 -lt $region1 + 65536 -and $region1 -lt $region0 + 131072)) { throw 'Blocchi kernel sovrapposti o non allineati.' }
        for ($slot = 0; $slot -lt 4; ++$slot) {
            for ($page = 0; $page -lt 34; ++$page) {
                $address = $regionPte + ($slot * 34 + $page) * 8
                $line = [regex]::Match($registers, '(?im)^' + $address.ToString('x16') + ':\s+0x([0-9a-f]{16})')
                # QEMU stampa due qword per riga: il secondo valore usa la riga precedente.
                if (!$line.Success) {
                    $line = [regex]::Match($registers, '(?im)^' + ($address - 8).ToString('x16') + ':\s+0x[0-9a-f]{16}\s+0x([0-9a-f]{16})')
                }
                if (!$line.Success) { throw 'PTE arena assente dal monitor.' }
                $actual = [Convert]::ToUInt64($line.Groups[1].Value,16)
                $pages = if ($slot -eq 0 -and $case.Variant -ne 'region-retired') {32} elseif ($slot -eq 1) {16} else {0}
                if ($page -gt 0 -and $page -le $pages) {
                    $physical = $(if ($slot -eq 0) {$region0} else {$region1}) + ($page - 1) * 4096
                    $expectedPte = $physical -bor [uint64]::Parse('8000000000000003',[Globalization.NumberStyles]::HexNumber)
                    if (($actual -band [uint64]::Parse('ffffffffffffff9f',[Globalization.NumberStyles]::HexNumber)) -ne $expectedPte) { throw 'PTE arena RW NX diversa dal lease.' }
                } elseif ($actual) { throw 'Guardia o coda arena presente.' }
            }
        }
        $idt = [regex]::Match($log, '(?i)KERNEL_IRQ_IDT_BASE: 0x([0-9a-f]{16})')
        if (!$idt.Success -or $registers -notmatch ('(?i)IDT=\s*' + $idt.Groups[1].Value + '\s+00000fff')) {
            throw 'IDTR attivo diverso dalla tabella kernel verificata.'
        }
        $idtBase = [Convert]::ToUInt64($idt.Groups[1].Value,16)
        if ($idtBase -lt $OsImageBase -or $idtBase -ge $OsImageLimit -or ($idtBase -band 15)) { throw 'IDT fuori dal kernel o non allineata.' }
        $gdt = [regex]::Match($log, '(?i)KERNEL_GDT_BASE: 0x([0-9a-f]{16})')
        $tss = [regex]::Match($log, '(?i)KERNEL_TSS_BASE: 0x([0-9a-f]{16})')
        if (!$gdt.Success -or !$tss.Success -or
            $registers -notmatch ('(?i)GDT=\s*' + $gdt.Groups[1].Value + '\s+00000027') -or
            $registers -notmatch ('(?i)TR =0018\s+' + $tss.Groups[1].Value + '\s+00000067\s+00008900')) {
            throw 'GDTR o cache TR diversi dallo storage kernel.'
        }
        $gdtBase = [Convert]::ToUInt64($gdt.Groups[1].Value,16)
        $tssBase = [Convert]::ToUInt64($tss.Groups[1].Value,16)
        if ($gdtBase -lt $OsImageBase -or $gdtBase -ge $OsImageLimit -or ($gdtBase -band 15) -or
            $tssBase -ne $gdtBase + 48) { throw 'Storage GDT TSS fuori dal kernel.' }
        $descriptor = [regex]::Match($registers, '(?im)^' + ($gdtBase + 24).ToString('x16') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
        if (!$descriptor.Success -or ([Convert]::ToUInt64($descriptor.Groups[1].Value,16) -shr 40 -band 255) -ne 0x8b) {
            throw 'LTR non ha marcato busy il descrittore TSS in memoria.'
        }
        foreach ($stack in @(1,2,3)) {
            $top = [regex]::Match($log, '(?i)KERNEL_IST' + $stack + '_TOP: 0x([0-9a-f]{16})')
            $expectedTop = 0x700000045000 + ($stack - 1)*5*4096
            if (!$top.Success -or [Convert]::ToUInt64($top.Groups[1].Value,16) -ne $expectedTop) {
                throw 'Geometria IST diversa dal profilo.'
            }
        }
        $pteBase = [Convert]::ToUInt64($istPte.Groups[1].Value,16)
        $physicalMatch = [regex]::Match($log, '(?i)KERNEL_IST_PHYSICAL: 0x([0-9a-f]{16})')
        if (!$physicalMatch.Success) { throw 'Lease fisico IST assente.' }
        $istPhysical = [Convert]::ToUInt64($physicalMatch.Groups[1].Value,16)
        $nx = [Convert]::ToUInt64('8000000000000003',16)
        $mask = [Convert]::ToUInt64('ffffffffffffff9f',16)
        $mainPhysicalMatch = [regex]::Match($log, '(?i)KERNEL_MAIN_PHYSICAL: 0x([0-9a-f]{16})')
        if (!$mainPhysicalMatch.Success) { throw 'Lease principale assente.' }
        $mainPhysical = [Convert]::ToUInt64($mainPhysicalMatch.Groups[1].Value,16)
        $oldPhysical = Read-Hex $log '(?i)KERNEL_OLD_STACK_PHYSICAL: 0x([0-9a-f]{16})'
        $mainRsp = Read-Hex $log '(?i)KERNEL_MAIN_RSP: 0x([0-9a-f]{16})'
        if (($mainPhysical -lt $oldPhysical + 65536 -and $oldPhysical -lt $mainPhysical + 65536) -or
            $mainRsp -lt 0x700000061000 -or $mainRsp -ge 0x700000071000 -or ($mainRsp -band 7)) {
            throw 'Overlap fisico o RSP della continuazione errato.'
        }
        $mainBase = [Convert]::ToUInt64($mainPte.Groups[1].Value,16)
        $oldBase = [Convert]::ToUInt64($oldPte.Groups[1].Value,16)
        if ($mainBase -ne $oldBase + 96*8 -or
            $registers -notmatch ('(?im)^' + ([Convert]::ToUInt64($tss.Groups[1].Value,16) + 4).ToString('x16') + ':\s+0x0000700000071000')) {
            throw 'RSP0 o ramo paging dello stack principale errati.'
        }
        for ($slot = 0; $slot -lt 18; $slot += 2) {
            foreach ($base in @($mainBase,$oldBase)) {
                $row = [regex]::Match($registers, '(?im)^' + ($base + $slot*8).ToString('x16') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
                if (!$row.Success) { throw 'PTE principali non leggibili.' }
                for ($column = 0; $column -lt 2; ++$column) {
                    $index = $slot + $column
                    $actual = [Convert]::ToUInt64($row.Groups[$column + 1].Value,16)
                    $expectedPte = if ($base -eq $oldBase -or $index -eq 0 -or $index -eq 17) { [uint64]0 } else { ($mainPhysical + ($index - 1)*4096) -bor $nx }
                    if (($expectedPte -eq 0 -and $actual -ne 0) -or
                        ($expectedPte -ne 0 -and ($actual -band $mask) -ne $expectedPte)) { throw 'Mapping principale o revoca loader errati.' }
                }
            }
        }
        for ($slot = 0; $slot -lt 16; $slot += 2) {
            $row = [regex]::Match($registers, '(?im)^' + ($pteBase + $slot*8).ToString('x16') + ':\s+0x([0-9a-f]{16})\s+0x([0-9a-f]{16})')
            if (!$row.Success) { throw 'PTE IST non leggibili dal monitor.' }
            for ($column = 0; $column -lt 2; ++$column) {
                $index = $slot + $column
                $actual = [Convert]::ToUInt64($row.Groups[$column + 1].Value,16)
                $ordinal = [int][Math]::Floor($index / 5) * 4 + $index % 5
                $expectedPte = if ($index % 5 -eq 0) { [uint64]0 } else { ($istPhysical + ($ordinal - 1)*4096) -bor $nx }
                if (($expectedPte -eq 0 -and $actual -ne 0) -or
                    ($expectedPte -ne 0 -and ($actual -band $mask) -ne $expectedPte)) { throw 'Guardia o permessi IST errati.' }
            }
        }
        if ($nestedCase) {
            $rspMatch = [regex]::Match($registers, 'RSP=([0-9a-f]{16})')
            if (!$rspMatch.Success) { throw 'Bounds IST assenti nel fault annidato.' }
            $nestedRsp = [Convert]::ToUInt64($rspMatch.Groups[1].Value,16)
            if ($registers -notmatch 'RAX=4b4e455354450001' -or $registers -notmatch 'SS =0000' -or
                $registers -notmatch 'CR2=0000700000020000' -or $nestedRsp -ne 0x700000045000 - 56 -or
                $registers -notmatch ('(?im)^' + $nestedRsp.ToString('x16') + ':\s+0x000000000000000e\s+0x0000000000000000') -or
                $registers -notmatch ('(?im)^' + ($nestedRsp + 48).ToString('x16') + ':\s+0x0000000000000000') -or
                ([regex]::Matches($log,'(?m)^\[ FAIL \] KERNEL_EXCEPTION\r?$')).Count -ne 1 -or
                $log.Contains('KERNEL_EXCEPTION_HALTED') -or $log.Contains('KERNEL_READY')) {
                throw 'Secondo fault non arrestato prima del rientro in C.'
            }
        } elseif (!$faultCase) {
            $terminalRsp = Read-Hex $registers 'RSP=([0-9a-f]{16})'
            if ($registers -notmatch ('(?i)RAX=' + $marker) -or $registers -notmatch 'SS =0010' -or
                $terminalRsp -lt 0x700000061000 -or $terminalRsp -ge 0x700000071000 -or
                ($case.Variant -eq 'main-return' -and $log.Contains('KERNEL_READY'))) { throw 'Arresto kernel errato.' }
        } else {
            $rsp = Read-Hex $registers 'RSP=([0-9a-f]{16})'
            $errorCode = switch ($case.Variant) { 'execute' {17} 'main-execute' {17} 'region-execute' {17} 'selector' {40} 'stack' {2} default {0} }
            $vector = if ($case.Nmi) { 2 } else { switch ($case.Variant) { 'opcode' {6} 'selector' {13} 'divide' {0} 'double' {8} default {14} } }
            $istTop = if ($vector -eq 2) { 0x70000004a000 } elseif ($vector -eq 8) { 0x70000004f000 } else { 0x700000045000 }
            $faultAddress = switch ($case.Variant) {
                'guard-low' { [Convert]::ToUInt64('70000001f000',16) }
                'guard-high' { [Convert]::ToUInt64('700000030000',16) }
                'stack' { [Convert]::ToUInt64('700000060ff8',16) }
                'main-retired' { [Convert]::ToUInt64('700000001000',16) }
                'main-guard-high' { [Convert]::ToUInt64('700000071000',16) }
                'main-execute' { [Convert]::ToUInt64('700000070000',16) }
                'region-retired' { [Convert]::ToUInt64('700000081000',16) }
                'region-guard-low' { [Convert]::ToUInt64('700000080000',16) }
                'region-guard-high' { [Convert]::ToUInt64('7000000a1000',16) }
                'region-execute' { [Convert]::ToUInt64('7000000a0000',16) }
                default { [Convert]::ToUInt64('70000002f000',16) }
            }
            $frameAddress = Read-Hex $log '(?i)FAULT_FRAME: 0x([0-9a-f]{16})'
            $faultRip = Read-Hex $log '(?i)FAULT_RIP: 0x([0-9a-f]{16})'
            $faultRsp = Read-Hex $log '(?i)FAULT_RSP: 0x([0-9a-f]{16})'
            $faultFlags = Read-Hex $log '(?i)FAULT_FLAGS: 0x([0-9a-f]{16})'
            if ((Read-Hex $registers 'RAX=([0-9a-f]{16})') -ne [Convert]::ToUInt64('4b4641554c540001',16) -or
                (Read-Hex $registers 'RIP=([0-9a-f]{16})') -lt $OsImageBase -or
                (Read-Hex $registers 'RIP=([0-9a-f]{16})') -ge $OsImageLimit -or
                (Read-Hex $log '(?i)FAULT_VECTOR: 0x([0-9a-f]{16})') -ne $vector -or
                (Read-Hex $log '(?i)FAULT_ERROR: 0x([0-9a-f]{16})') -ne $errorCode -or
                (Read-Hex $log '(?i)FAULT_CS: 0x([0-9a-f]{16})') -ne 8 -or
                (Read-Hex $log '(?i)FAULT_SS: 0x([0-9a-f]{16})') -ne 16 -or
                $faultRsp -lt 0x700000061000 -or $faultRsp -ge 0x700000071000 -or
                ($faultFlags -band 512) -ne 0 -or
                $frameAddress -ne $istTop - 56 -or
                $registers -notmatch 'SS =0000' -or $rsp -lt $istTop - 16384 -or $rsp -ge $istTop -or
                $registers -notmatch ('(?im)^' + $frameAddress.ToString('x16') + ':\s+0x' + $vector.ToString('x16') + '\s+0x' + $errorCode.ToString('x16')) -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 16).ToString('x16') + ':\s+0x' + $faultRip.ToString('x16') + '\s+0x0000000000000008') -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 32).ToString('x16') + ':\s+0x' + $faultFlags.ToString('x16') + '\s+0x' + $faultRsp.ToString('x16')) -or
                $registers -notmatch ('(?im)^' + ($frameAddress + 48).ToString('x16') + ':\s+0x0000000000000010') -or
                (!$case.Nmi -and $log.Contains('KERNEL_READY')) -or $log.Contains('EXCEPTION_FRAME_INVALID') -or
                $log.Contains('EXPECTED_CPU_FAULT_MISSING') -or $log.Contains('EXPECTED_WINDOW_FAULT_MISSING') -or
                $log.Contains('EXPECTED_MAIN_FAULT_MISSING') -or $log.Contains('EXPECTED_REGION_FAULT_MISSING')) { throw "Eccezione kernel errata: $scenario" }
            if ($vector -eq 14 -and ((Read-Hex $registers 'CR2=([0-9a-f]{16})') -ne $faultAddress -or
                (Read-Hex $log '(?i)FAULT_CR2: 0x([0-9a-f]{16})') -ne $faultAddress -or
                ($case.Variant -in @('execute','main-execute','region-execute') -and $faultRip -ne $faultAddress))) { throw 'Diagnostica CR2 o RIP NX errata.' }
            if ($vector -ne 14 -and $vector -ne 8 -and ($faultRip -lt $OsImageBase -or $faultRip -ge $OsImageLimit)) { throw 'RIP fault fuori dal kernel.' }
            if ($raw -notmatch '\x1b\[31m\[ FAIL \]\x1b\[0m KERNEL_EXCEPTION\r?\n') { throw 'Colore etichetta eccezione errato.' }
        }
        $pages = [regex]::Match($log, 'Pagine convenzionali candidate: 0x([0-9a-f]{16})')
        if (!$pages.Success -or [Convert]::ToUInt64($pages.Groups[1].Value, 16) -eq 0) { throw 'Inventario RAM vuoto.' }
        $candidatePages[$case.Name] = [Convert]::ToUInt64($pages.Groups[1].Value, 16)
        $managed = [regex]::Match($log, 'Pagine fisiche gestite: 0x([0-9a-f]{16})')
        if (!$managed.Success -or [Convert]::ToUInt64($managed.Groups[1].Value, 16) -eq 0 -or
            [Convert]::ToUInt64($managed.Groups[1].Value, 16) -gt $candidatePages[$case.Name]) { throw 'Contabilita fisica errata.' }
        $tail = $raw.Substring($raw.IndexOf('# ============ ingresso kernel x86'))
        if ($tail -notmatch '\x1b\[32m\[  OK  \]\x1b\[0m KERNEL_CONTRACT_OK' -or
            ($case.Variant -eq 'panic' -and ($log.Contains('KERNEL_READY') -or
                $tail -notmatch '\x1b\[31m\[ FAIL \]\x1b\[0m INTENTIONAL_TEST_PANIC'))) {
            throw 'Etichette o percorso panic errati.'
        }
        Write-BootStatus OK "$($case.Name): mappatura, inventario e stato CPU terminale verificati."
    } finally {
        if ($started -and !$vm.HasExited) { $vm.Kill(); $vm.WaitForExit() }
        $vm.Dispose()
    }
}
if ($candidatePages['ready-256'] -le $candidatePages['ready-128']) { throw 'RAM aggiuntiva non riconosciuta.' }
& (Join-Path $PSScriptRoot 'serial/queue/scripts/monitor-test.ps1') -ScenarioDirectory (Join-Path $output 'ready-128')
Test-IrqRegistryMonitorRejections (Get-Content (Join-Path $output 'ready-128/serial.log') -Raw) (Get-Content (Join-Path $output 'ready-128/registers.log') -Raw)
Test-LapicTimerMonitorRejections (Get-Content (Join-Path $output 'ready-128/serial.log') -Raw) `
    (Get-Content (Join-Path $output 'ready-128/registers.log') -Raw) (Join-Path $output 'ready-128/qemu.log')
Write-BootStatus INFO "Log: $output"
Write-BootStatus OK '23/23 scenari: MADT con 1/4 CPU, ACPI, mapping, stack, guardie, NX, NMI e double fault verificati.'
