# ============ PS/2 component compilation ============

if ($TestPs2Controller -or $Ps2Input) {
    foreach ($source in @('kernel/drivers/input/ps2/controller/controller.c','kernel/drivers/input/ps2/controller/transport.c',
        'kernel/drivers/input/ps2/controller/discovery.c') +
        $(if ($TestPs2Controller -or $Ps2Mouse) { @('kernel/drivers/input/ps2/mouse/protocol.c') } else { @() })) {
        $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
        $objects += $object
    }
}
if ($TestPs2Controller -or $Ps2Input) {
    $sources = if ($Ps2Input) { @('route','interrupt','session','task') | ForEach-Object { 'arch/x86/kernel/drivers/input/ps2/runtime/' + $_ + '.c' } }
        else { @('arch/x86/kernel/drivers/input/ps2/integration/native.c') }
    if ($Ps2Input) { $sources += @('kernel/console/line/line.c','kernel/shell/shell.c',
        'kernel/shell/queue.c','arch/x86/kernel/drivers/input/ps2/shell/task.c',
        'arch/x86/kernel/drivers/input/ps2/runtime/consumer/stream.c',
        'arch/x86/kernel/drivers/input/ps2/runtime/consumer/recovery.c',
        'arch/x86/kernel/scheduler/irq/preempt.c','kernel/scheduler/preempt.c') }
    if ($Ps2Mouse) { $sources += @('kernel/drivers/input/ps2/mouse/decoder.c','kernel/drivers/input/ps2/mouse/stream/control.c',
        'arch/x86/kernel/drivers/input/ps2/mouse/session.c','arch/x86/kernel/drivers/input/ps2/mouse/task.c') }
    foreach ($source in $sources) {
        $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
        $objects += $object
    }
}

# ============ Mouse scalar Rust ABI ============

if ($Ps2Mouse) {
    $object = Join-Path $output 'ps2-mouse-rust.obj'
    Invoke-Compiler 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','--target=x86_64-pc-windows-msvc',
        '-C','panic=abort','-C','opt-level=2','-D','warnings',
        (Join-Path $project 'kernel/drivers/input/ps2/mouse/rust/ffi/gateway.rs'),'-o',$object)
    $objects += $object
}
