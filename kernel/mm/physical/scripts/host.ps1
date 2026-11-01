# ============ physical host dependency objects ============

function New-PhysicalHostSync([string]$Project, [string]$Output, [switch]$Threaded) {
    $root = [IO.Path]::GetFullPath($Project)
    . (Join-Path $root 'scripts/lib/common.ps1')
    $destination = Join-Path $Output 'physical-sync'
    New-Item -ItemType Directory -Force $destination | Out-Null
    $saved = Use-Msvc
    $result = @()
    try {
        foreach ($source in @('kernel/sync/lock.c','kernel/sync/acquire.c','kernel/time/clock.c',
            'test/kernel/mm/physical/synchronization/backend.c')) {
            $object = Join-Path $destination (($source -replace '/', '-') -replace '\.c$', '.obj')
            $flags = @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/Oi','/O2','/c',"/I$root",
                "$root/$source","/Fo$object")
            if ($Threaded) { $flags += '/DPHYSICAL_THREAD_TEST=1' }
            Invoke-Checked "$($saved.Bin)/cl.exe" $flags | Out-Host
            $result += $object
        }
        $object = Join-Path $destination 'atomics.obj'
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
            '-C','panic=abort','-C','opt-level=2',"$root/kernel/sync/rust/atomics/operations.rs",'-o',$object) | Out-Host
        $result += $object
    } finally { Restore-Msvc $saved }
    return $result
}
