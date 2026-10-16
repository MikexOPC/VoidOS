# ============ pointer Rust checks ============

Invoke-Checked $Python @('-B',"$project/test/kernel/console/video/pointer/firmware_test.py")

foreach ($profile in @('debug','release')) {
    $optimization = if ($profile -eq 'release') { '2' } else { '0' }
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
        '-C','overflow-checks=yes',"$project/test/kernel/console/video/pointer/rust/movement/plan_test.rs",
        '-o',"$out/pointer-$profile.exe")
    Invoke-Checked "$out/pointer-$profile.exe" @()
}
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
    '--emit=metadata','-D','warnings',"$project/kernel/console/video/pointer/rust/movement/plan.rs",'-o',"$out/pointer-clippy.rmeta")
Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',
    "$project/kernel/console/video/pointer/rust/ffi/movement.rs",'-o',"$out/pointer-plan.obj")

# ============ pointer C ASan ============

Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
    ("/I$project"),"$project/kernel/console/video/pointer/state.c","$project/kernel/console/video/pointer/shape.c",
    "$project/kernel/console/video/console.c","$project/test/kernel/console/video/pointer/state_test.c",
    ("/Fo$out/"),("/Fd$out/pointer.pdb"),("/Fe$out/pointer.exe"),'/link',"$out/pointer-plan.obj","$out/plan.obj",'/incremental:no','/DEBUG','/WX')
Invoke-Checked "$out/pointer.exe" @()
