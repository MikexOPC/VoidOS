# ============ inactive installation diagnostic only ============

$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
Invoke-Compiler $tool.Nasm @('-f','bin','-Wall','-Werror','-o',"$output/ap-template.bin",
    "$project/arch/x86/kernel/cpu/percpu/startup/assembly/trampoline/entry.asm")
if ((Get-Item "$output/ap-template.bin").Length -ne 4096) { throw 'Invalid AP template size' }
foreach ($source in @('arch/x86/kernel/cpu/percpu/startup/assembly/template/image.asm',
    'test/arch/x86/kernel/cpu/percpu/startup/installation/probe/assembly/terminal/entry.asm')) {
    $object = Join-Path $output (($source -replace '/','-') -replace '\.asm$','.obj')
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror',"-I$output/",'-o',$object,"$project/$source")
    $objects += $object
}
if ($TestApLaunch) {
    if ($TestApGroupPanic -ne 'none') {
        $source = 'test/arch/x86/kernel/cpu/percpu/panic/integration/group.c'
        $object = Join-Path $output 'ap-group-panic-probe.obj'
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
    foreach ($source in @('arch/x86/kernel/cpu/percpu/startup/transport/wake.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/prepare.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/io.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/run.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/rearm.c',
        'test/arch/x86/kernel/cpu/percpu/startup/commit/probe/launch.c')) {
        $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
    $object = Join-Path $output 'ap-gate-load.obj'
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror','-o',$object,
        "$project/arch/x86/kernel/cpu/percpu/startup/transport/assembly/gate/load.asm")
    $objects += $object
}
if ($TestApTransition -eq 'joint-panic') {
    foreach ($source in @('arch/x86/kernel/cpu/percpu/admission/group.c',
        'arch/x86/kernel/cpu/percpu/admission/native.c',
        'arch/x86/kernel/cpu/percpu/admission/entry/entry.c',
        'arch/x86/kernel/cpu/percpu/startup/private/return.c')) {
        $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
    $object = Join-Path $output 'ap-admission-policy.obj'
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
        "$project/arch/x86/kernel/cpu/percpu/admission/rust/snapshot/policy.rs",'-o',$object)
    $objects += $object
    foreach ($part in @('arch/x86/kernel/cpu/percpu/startup/assembly/private/entry.asm',
        'arch/x86/kernel/cpu/percpu/startup/assembly/private/resume.asm',
        'arch/x86/kernel/cpu/percpu/admission/assembly/entry/bridge.asm')) {
        $object = Join-Path $output (($part -replace '/','-') -replace '\.asm$','.obj')
        Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror','-o',$object,"$project/$part")
        $objects += $object
    }
}
foreach ($source in @('arch/x86/kernel/cpu/percpu/startup/entry.c',
    'arch/x86/kernel/cpu/percpu/startup/blob/package.c',
    'arch/x86/kernel/cpu/percpu/startup/installation/install.c',
    'test/arch/x86/kernel/cpu/percpu/startup/installation/probe/probe.c')) {
    $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
    $objects += $object
}
$object = Join-Path $output 'ap-startup-protocol.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp','-D','warnings',
    "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',$object)
$objects += $object
if ($TestApTransition -like 'joint*') {
    foreach ($source in @('arch/x86/kernel/cpu/percpu/startup/commit/commit.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/native.c',
        'test/arch/x86/kernel/cpu/percpu/startup/commit/probe/probe.c')) {
        $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
}
