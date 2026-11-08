param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../..").Path
if (!$OutputDirectory) {
    $OutputDirectory=Join-Path $project ('kernel/build/lib-host-' + [guid]::NewGuid().ToString('N'))
}
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../scripts/lib/common.ps1')

# ============ shared library host checks ============

$msvc=Use-Msvc
try {
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    $suites=[ordered]@{
        geometry=@('lib/geometry/rect.c','lib/geometry/damage.c','test/lib/geometry/geometry_test.c')
        containers=@('lib/containers/ring.c','lib/containers/bitmap.c','test/lib/containers/containers_test.c')
        memory=@('lib/memory/address_ranges.c','test/lib/memory/address_ranges_test.c','test/lib/memory/host.c')
    }
    $paths=@()
    foreach ($name in $suites.Keys) {
        $sources=@($suites[$name] | ForEach-Object {"$project/$_"})
        $paths+=$sources
        New-Item -ItemType Directory -Force "$out/$name" | Out-Null
        Invoke-Checked "$($msvc.Bin)/cl.exe" ($flags+$sources+@("/Fo$out/$name/","/Fd$out/$name/host.pdb","/Fe$out/$name/host.exe",'/link','/incremental:no'))
        Invoke-Checked "$out/$name/host.exe" @()
    }
    $capacityTest=Join-Path $project 'test/lib/memory/rust/capacity/buffer_capacity_test.rs'
    $rustFlags=@('--edition=2024','--test','--target','x86_64-pc-windows-msvc','-D','warnings')
    Invoke-Checked 'rustc' ($rustFlags + @('-C','overflow-checks=yes',$capacityTest,'-o',"$out/memory/capacity.exe"))
    Invoke-Checked "$out/memory/capacity.exe" @()
    Invoke-Checked 'clippy-driver' ($rustFlags + @('--emit=metadata',$capacityTest,'-o',"$out/memory/capacity.rmeta"))
    Invoke-Cppcheck $project $out $paths
} finally {
    Restore-Msvc $msvc
}
"PASS: lib geometry, containers and memory evidence $out"
