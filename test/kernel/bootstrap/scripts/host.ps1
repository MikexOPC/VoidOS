$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$out = Join-Path $project ('kernel/build/bootstrap-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ ordinary bootstrap and failure paths ============

try {
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    & "$bin/cl.exe" /nologo /TC /std:c11 /W4 /WX /Zi /fsanitize=address /MD /O2 ("/I$project") `
        "$project/test/kernel/mm/physical/synchronization/reader_model.c" /DPHYSICAL_MODEL_EXTERNAL_READ=1 "$project/kernel/bootstrap/memory.c" "$project/test/kernel/bootstrap/ordinary_test.c" `
        ("/Fo$out/") ("/Fd$out/test.pdb") ("/Fe$out/test.exe") /link /incremental:no
    if ($LASTEXITCODE) { throw 'Host compilation failed' }
    & "$out/test.exe"
    if ($LASTEXITCODE) { throw 'Host bootstrap test failed' }
    $cppcheck="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") "$project/test/kernel/mm/physical/synchronization/reader_model.c" /DPHYSICAL_MODEL_EXTERNAL_READ=1 "$project/kernel/bootstrap/memory.c" "$project/test/kernel/bootstrap/probes.c" `
        "$project/test/kernel/bootstrap/ordinary_test.c" "$project/kernel/main.c" "$project/kernel/startup/early.c" "$project/kernel/startup/platform.c" "$project/kernel/startup/devices.c" "$project/kernel/startup/profile.c" 2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml=Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
    "PASS: bootstrap host evidence $out"
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
