# ============ pointer movement validation ============

if ($ConsoleShell) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=staticlib',
        '--target','x86_64-pc-windows-msvc',
        '-C','panic=abort','-C','opt-level=2','-C','lto=fat','-C','codegen-units=1',
        '-C','overflow-checks=yes','-D','warnings',
        (Join-Path $project 'kernel/console/video/rust/ffi/requests.rs'),
        '-o',($output + '/console-requests.lib'))
    $objects += ($output + '/console-requests.lib')
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj',
        '--target','x86_64-pc-windows-msvc','-C','panic=abort','-C','opt-level=2',
        '-C','overflow-checks=yes','-D','warnings',
        (Join-Path $project 'kernel/console/video/pointer/rust/ffi/movement.rs'),
        '-o',($output + '/pointer-plan.obj'))
    $objects += ($output + '/pointer-plan.obj')
}
