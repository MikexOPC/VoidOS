# ============ installation and admission for diagnostics or ordinary boot ============

$tool = if ($PlanOnly) { @{Nasm = 'nasm.exe'} } else {
    & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
}
Invoke-Compiler $tool.Nasm @('-f','bin','-Wall','-Werror','-o',"$output/ap-template.bin",
    "$project/arch/x86/kernel/cpu/percpu/startup/assembly/trampoline/entry.asm")
if (!$PlanOnly) { Assert-ApTemplateSize "$output/ap-template.bin" }

$templateSources = @('arch/x86/kernel/cpu/percpu/startup/assembly/template/image.asm')
if (!$ApRuntime) { $templateSources += 'test/arch/x86/kernel/cpu/percpu/startup/installation/probe/assembly/terminal/entry.asm' }
foreach ($source in $templateSources) {
    $object = Join-Path $output (($source -replace '/','-') -replace '\.asm$','.obj')
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror',"-I$output/",'-o',$object,"$project/$source")
    $objects += $object
}
if ($TestApLaunch -or $ApRuntime) {
    if ($TestApGroupPanic -ne 'none') {
        $source = 'test/arch/x86/kernel/cpu/percpu/panic/integration/group.c'
        $object = Join-Path $output 'ap-group-panic-probe.obj'
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
    $transportSources = @('arch/x86/kernel/cpu/percpu/startup/transport/wake.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/prepare.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/io.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/run.c',
        'arch/x86/kernel/cpu/percpu/startup/transport/native/rearm.c')
    $transportSources += $(if ($ApRuntime) { 'arch/x86/kernel/cpu/percpu/startup/runtime/runtime.c' } else { 'test/arch/x86/kernel/cpu/percpu/startup/commit/probe/launch.c' })
    foreach ($source in $transportSources) {
        $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
    $object = Join-Path $output 'ap-gate-load.obj'
    Invoke-Compiler $tool.Nasm @('-f','win64','-Wall','-Werror','-o',$object,
        "$project/arch/x86/kernel/cpu/percpu/startup/transport/assembly/gate/load.asm")
    $objects += $object
}
if ($TestApTransition -eq 'joint-panic' -or $ApRuntime) {
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
        $serviceDefines = if ($ApRuntime) { @('-DKERNEL_AP_SERVICE=1') } else { @() }
        Invoke-Compiler $tool.Nasm (@('-f','win64','-Wall','-Werror','-o',$object,"$project/$part") + $serviceDefines)
        $objects += $object
    }
}
if ($ApRuntime) {
    . (Join-Path $project 'arch/x86/kernel/cpu/percpu/service/scripts/build.ps1')
}
$installationSources = @('arch/x86/kernel/cpu/percpu/startup/entry.c',
    'arch/x86/kernel/cpu/percpu/startup/blob/package.c',
    'arch/x86/kernel/cpu/percpu/startup/installation/install.c')
if (!$ApRuntime) { $installationSources += 'test/arch/x86/kernel/cpu/percpu/startup/installation/probe/probe.c' }
foreach ($source in $installationSources) {
    $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
    $objects += $object
}
$object = Join-Path $output 'ap-startup-protocol.obj'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp','-D','warnings',
    "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',$object)
$objects += $object
if ($TestApTransition -like 'joint*' -or $ApRuntime) {
    $commitSources = @('arch/x86/kernel/cpu/percpu/startup/commit/commit.c',
        'arch/x86/kernel/cpu/percpu/startup/commit/native.c')
    if (!$ApRuntime) { $commitSources += 'test/arch/x86/kernel/cpu/percpu/startup/commit/probe/probe.c' }
    foreach ($source in $commitSources) {
        $object = Join-Path $output (($source -replace '/','-') -replace '\.c$','.obj')
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @("$project/$source","/Fo$object"))
        $objects += $object
    }
}
