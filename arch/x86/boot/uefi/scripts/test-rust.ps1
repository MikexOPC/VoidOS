param([string]$ToolchainBin)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'status.ps1')
try {

    # ====== strumenti e cartella degli artefatti ====== #

    if (!$ToolchainBin) {
        $base = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
        $version = Get-ChildItem -LiteralPath $base -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if (!$version) { throw 'Specificare -ToolchainBin con cl.exe x64.' }
        $ToolchainBin = Join-Path $version.FullName 'bin\Hostx64\x64'
    }
    $compiler = Join-Path $ToolchainBin 'cl.exe'
    $compilerInclude = (Resolve-Path (Join-Path $ToolchainBin '..\..\..\include')).Path
    $bootRoot = Split-Path -Parent $PSScriptRoot
    $projectRoot = (Resolve-Path (Join-Path $bootRoot '..\..\..\..')).Path
    $output = Join-Path $bootRoot 'build\rust-tests'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $rustc = (Get-Command rustc -ErrorAction Stop).Source

    function Invoke-RustChecked([string]$Program, [string[]]$Arguments) {
        $messages = & $Program @Arguments 2>&1
        $code = $LASTEXITCODE
        foreach ($message in $messages) {
            Write-BootStatus $(if ($code -eq 0) { 'INFO' } else { 'FAIL' }) "$message"
        }
        if ($code -ne 0) { throw "Errore in $Program ($code)" }
    }
    Write-BootSection 'compilazione test Rust e confine C'
    Invoke-RustChecked $rustc @('--version')
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/GS-', '/Zl', '/Od', '/c', ('/I' + $bootRoot), ('/I' + $projectRoot), ('/I' + $compilerInclude))
    $objects = @()
    foreach ($source in @('paging\plan.c', 'paging\tables.c', 'test\paging\rust\bridge.c')) {
        $object = Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
        Invoke-RustChecked $compiler ($flags + @((Join-Path $bootRoot $source), ('/Fo' + $object)))
        $objects += $object
    }
    $rangeObject = Join-Path $output 'address_ranges.obj'
    Invoke-RustChecked $compiler ($flags + @((Join-Path $projectRoot 'lib\memory\address_ranges.c'), ('/Fo' + $rangeObject)))
    $objects += $rangeObject

    # ====== test host senza dipendenze esterne ====== #

    $executable = Join-Path $output 'allocation_tests.exe'
    $arguments = @('--test', '--edition=2024', '--target', 'x86_64-pc-windows-msvc', '-D', 'warnings',
                   (Join-Path $bootRoot 'test\paging\rust\allocation_tests.rs'), '-o', $executable)
    foreach ($object in $objects) { $arguments += @('-C', ('link-arg=' + $object)) }
    Invoke-RustChecked $rustc $arguments
    Write-BootSection 'test Rust delle allocazioni C'
    Invoke-RustChecked $executable @('--test-threads=4')
    Write-BootStatus OK 'Test Rust superati: ABI, permessi, ownership, errori e 512 sequenze riproducibili.'
} catch {
    Write-BootStatus FAIL $_.Exception.Message
    throw
}
