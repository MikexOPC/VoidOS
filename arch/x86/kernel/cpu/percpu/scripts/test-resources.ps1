param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-resources-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ geometry and actual C lifecycle with modeled machine ============

$rules = "$project/arch/x86/kernel/cpu/percpu/resources/rust/geometry/pages.rs"
foreach ($mode in @('debug','release')) {
    $options = @('--edition=2024','--test','-D','warnings',$rules,'-o',"$out/rust-$mode.exe")
    if ($mode -eq 'release') { $options += @('-C','opt-level=2') }
    Invoke-Checked rustc $options
    Invoke-Checked "$out/rust-$mode.exe" @()
}
$objects = @()
foreach ($source in @('arch/x86/kernel/cpu/percpu/resources/rust/geometry/pages.rs','kernel/mm/rust/regions/validation.rs')) {
    $object = "$out/" + ($source -replace '/','-') + '.obj'
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$source",'-o',$object)
    $objects += $object
}
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/resources/lifecycle.c','arch/x86/kernel/cpu/percpu/resources/construct.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/retention.c',
        'arch/x86/kernel/cpu/percpu/resources/descriptors.c','arch/x86/kernel/cpu/percpu/resources/paging/registry.c',
        'arch/x86/lib/descriptors/gdt.c','arch/x86/lib/descriptors/tss.c','lib/memory/address_ranges.c',
        'test/arch/x86/kernel/cpu/percpu/resources/model.c','test/arch/x86/kernel/cpu/percpu/resources/cases.c') |
        ForEach-Object { Join-Path $project $_ }
    foreach ($source in $sources) {
        $object = "$out/" + (($source.Substring($project.Length + 1) -replace '[\\/]','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/UNDEBUG','/c',"/I$project",$source,"/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") + $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out $sources
} finally { Restore-Msvc $saved }
"PASS: AP resources host evidence $out"
