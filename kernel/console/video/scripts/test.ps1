param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../..").Path
$out = if ($OutputDirectory) { $OutputDirectory } else { "$project/kernel/build/console-grid-host" }
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked execution ============

function Invoke-Checked([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE) { throw "Failed: $program" }
}

# ============ supported host ABI ============

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
    $rustc="$env:USERPROFILE/.cargo/bin/rustc.exe"
    foreach ($profile in @('debug','release')) {
        $optimization = if ($profile -eq 'release') { '2' } else { '0' }
        Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
            '-C','overflow-checks=yes',"$project/kernel/console/video/test/rust/cursor/plan_test.rs",
            '-o',"$out/$profile.exe")
        Invoke-Checked "$out/$profile.exe" @()
    }
    Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
        '--emit=metadata','-D','warnings',"$project/kernel/console/video/rust/cursor/plan.rs",'-o',"$out/clippy.rmeta")
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',
        "$project/kernel/console/video/rust/cursor/plan.rs",'-o',"$out/plan.obj")
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/kernel/console/video/console.c","$project/kernel/console/video/render.c",
        "$project/kernel/console/video/test/host/cases.c","$project/kernel/console/video/test/host/render_cases.c",
        ("/Fo$out/"),("/Fd$out/host.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/fsanitize=address','/MD',
        "$out/console.obj","$out/render.obj","$out/cases.obj","$out/render_cases.obj","$out/plan.obj",("/Fe$out/host.exe"),
        '/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/host.exe" @()
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',
        "$project/arch/x86/kernel/drivers/video/framebuffer/rust/drawing/validation.rs",'-o',"$out/drawing.obj")
    $gatewayObjects = @("$out/drawing.obj")
    foreach ($source in @('gateway/cell.c','test/gateway/cases.c','font/font.c')) {
        $object = "$out/gateway-" + $source.Replace('/','-').Replace('.c','.obj')
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
            ("/I$project"),"$project/arch/x86/kernel/drivers/video/framebuffer/$source",("/Fo$object"),("/Fd$out/host.pdb"))
        $gatewayObjects += $object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/gateway.exe")) +
        $gatewayObjects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/gateway.exe" @()
    $cppcheck = "$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") "$project/kernel/console/video/console.c" `
        "$project/kernel/console/video/render.c" "$project/kernel/console/video/test/host/render_cases.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/gateway/cell.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/gateway/native.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/test/gateway/cases.c" `
        "$project/kernel/console/video/test/kernel/render_probe.c" `
        "$project/kernel/console/video/test/host/cases.c" "$project/kernel/console/video/test/kernel/probe.c" `
        2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml = Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: console grid host evidence $out"
