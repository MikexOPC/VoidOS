param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/framebuffer-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$module = Join-Path $project 'kernel/drivers/video/framebuffer'
$boot = Join-Path $project 'arch/x86/boot/uefi'

# ============ checked tools ============

. (Join-Path $PSScriptRoot '../../../../../scripts/lib/common.ps1')
function Invoke-Checked([string]$Program, [string[]]$Arguments) { Invoke-OsChecked $Program $Arguments "$out/tools.log" -Tee }

# ============ Rust validators and ABI ============

foreach ($component in @('mode', 'pixel', 'mapping')) {
$source = "$module/rust/$component/validation.rs"
if ($component -eq 'mapping') { $source = "$module/rust/mapping/geometry.rs" }
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        $source,'-o',"$out/$component-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/$component-$mode.exe" @()
}
Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings','--emit=metadata',
    $source,'-o',"$out/$component.rmeta")
}
$gateway = "$module/rust/ffi/mode.rs"
Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',$gateway,'-o',"$out/mode.obj")
Invoke-Checked 'clippy-driver' @('--edition=2024','--crate-type=lib','-D','warnings',
    '--emit=metadata',$gateway,'-o',"$out/gateway.rmeta")
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH
try {
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$boot/graphics/gop.c","$boot/../../../../test/arch/x86/boot/uefi/graphics/selection_test.c",
        ("/Fo$out/"),("/Fd$out/host.pdb"),("/Fe$out/host.exe"),'/link','/incremental:no',"$out/mode.obj")
    Invoke-Checked "$out/host.exe" @()

    # ============ framebuffer handoff and memory exclusion ============

    Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',"$project/lib/memory/rust/capacity/buffer_capacity.rs",
        '-o',"$out/capacity.obj")
    $handoffObjects = @("$out/mode.obj","$out/capacity.obj")
    $handoffIndex = 0
    foreach ($handoffSource in @("$module/handoff.c","$boot/graphics/handoff.c",
        "$boot/memory/map.c","$project/lib/memory/address_ranges.c","$module/../../../../test/kernel/drivers/video/framebuffer/handoff/validation_test.c")) {
        $handoffObject = "$out/handoff-source-$handoffIndex.obj"
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',("/I$project"),("/I$boot"),$handoffSource,
            ("/Fo$handoffObject"),("/Fd$out/handoff.pdb"))
        $handoffObjects += $handoffObject
        ++$handoffIndex
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/handoff.exe")) +
        $handoffObjects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/handoff.exe" @()

    # ============ mapping geometry with transactional output ============

    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$module/mapping.c","$module/../../../../test/kernel/drivers/video/framebuffer/mapping/geometry_test.c",
        ("/Fo$out/"),("/Fd$out/mapping.pdb"),("/Fe$out/mapping.exe"),'/link','/incremental:no',"$out/mode.obj")
    Invoke-Checked "$out/mapping.exe" @()

    # ============ isolated EFI probe, production loader unchanged ============

    $objects = @("$out/mode.obj")
    foreach ($sourceFile in @('graphics/gop.c','../../../../test/arch/x86/boot/uefi/graphics/probe.c','console/log.c')) {
        $object = Join-Path $out (($sourceFile -replace '/','-') + '.obj')
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/O2','/Oi','/c',
            ("/I$project"),("/I$boot"),"$boot/$sourceFile",("/Fo$object"))
        $objects += $object
    }
    Invoke-Checked "$bin/link.exe" (@('/nologo','/machine:x64','/subsystem:efi_application',
        '/entry:efi_main','/nodefaultlib','/incremental:no','/dynamicbase:no','/fixed:no',
        ("/out:$out/BOOTX64.EFI")) + $objects)
    $cppcheck="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" "--quiet" --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") ("-I$boot") "$boot/graphics/gop.c" "$boot/../../../../test/arch/x86/boot/uefi/graphics/selection_test.c" `
        "$boot/../../../../test/arch/x86/boot/uefi/graphics/probe.c" "$boot/graphics/handoff.c" "$module/handoff.c" `
        "$module/../../../../test/kernel/drivers/video/framebuffer/handoff/validation_test.c" "$module/mapping.c" `
        "$module/../../../../test/kernel/drivers/video/framebuffer/mapping/geometry_test.c"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml=Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
    Get-FileHash "$out/BOOTX64.EFI" | ConvertTo-Json | Set-Content "$out/image.json"
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: framebuffer GOP host and EFI image $out"
