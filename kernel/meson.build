project('voidos-kernel', meson_version: '>=1.12.1')

fs = import('fs')

repo = fs.parent(meson.project_source_root())
out = meson.project_build_root()
planning = repo / 'scripts/tooling/build-plan/rust/plan'
tooling = repo / 'scripts/tooling/kernel-build/rust'
nasm = repo / 'kernel/build/tools/nasm-3.01/package/nasm-3.01/nasm.exe'

rustc = find_program('rustc')

# ====== user program target ======

sysroot = run_command(rustc, '--print', 'sysroot', check: true).stdout().strip()
if (get_option('profile') == 'ordinary' or get_option('profile').startswith('k6-process')) and not fs.is_dir(sysroot / 'lib/rustlib/x86_64-unknown-none')
    error('Target Rust x86_64-unknown-none assente: rustup target add x86_64-unknown-none')
endif

# ====== configure dependencies ======

foreach path : [
    planning / 'main.rs',
    planning / 'commands.tsv',
    repo / 'arch/x86/configs/layout.h',
    tooling / 'main.rs',
    tooling / 'plan.rs',
    tooling / 'step.rs',
    tooling / 'depfile.rs',
    tooling / 'image.rs',
    tooling / 'layout.rs',
    tooling / 'parity.rs',
    repo / 'scripts/tooling/qemu-runner/rust/evidence/sha256.rs',
    repo / 'arch/x86/kernel/exceptions/rust/frame/validation.rs',
    repo / 'arch/x86/kernel/descriptors/rust/layout/validation.rs',
    repo / 'arch/x86/mm/regions/rust/layout/validation.rs',
    repo / 'arch/x86/mm/paging/rust/validation/entries.rs',
]
    fs.read(path)
endforeach

planner = out / 'build-plan.exe'
tool = out / 'kernel_build.exe'

run_command(rustc, '--edition=2024', '-D', 'warnings', planning / 'main.rs', '-o', planner, check: true)
run_command(
    rustc, '--edition=2024', '-D', 'warnings', '-C', 'opt-level=2', '-C', 'overflow-checks=yes',
    tooling / 'main.rs', '-o', tool,
    check: true,
)

context = [
    '--planner', planner,
    '--repo', repo,
    '--out', out,
    '--rustc', rustc.full_path(),
    '--nasm', nasm,
    '--profile', get_option('profile'),
]
plan = run_command(tool, 'plan', context, check: true).stdout().strip()

# ====== plan rows ======

objects = []
ap_template = []
user_libs = []
user_programs = []
link_hash = ''

foreach line : plan.split('\n')
    fields = line.strip().split('\t')
    if fields[0] == 'link'
        link_hash = fields[2]
    else
        deps = []
        if fields[0] == 'after-template'
            deps = ap_template + user_programs
        elif fields[0] == 'after-lib'
            deps = user_libs
        endif
        step = custom_target(
            fields[1],
            output: fields[1],
            depfile: fields[2],
            command: [tool, 'run', fields],
            depends: deps,
        )
        objects += step
        if fields[1] == 'ap-template.bin'
            ap_template = [step]
        elif fields[1].endswith('.rlib')
            user_libs += step
        elif fields[1].endswith('.elf')
            user_programs += step
        endif
    endif
endforeach

linked = custom_target(
    'kernel.exe',
    output: ['kernel.exe', 'kernel.map'],
    command: [tool, 'link', out / 'plan.tsv', link_hash],
    depends: objects,
)

elf = custom_target(
    'kernel.elf',
    output: 'kernel.elf',
    command: [tool, 'image', out / 'kernel.exe', out / 'kernel.elf'],
    depends: linked,
    build_by_default: true,
)

if get_option('profile') == 'ordinary'
    cargo = find_program('cargo')
    slint_ui = custom_target(
        'user-ui.elf',
        output: 'user-ui.elf',
        input: files(
            repo / 'userspace/gui/ui/Cargo.toml',
            repo / 'userspace/gui/ui/build.rs',
            repo / 'userspace/gui/ui/desktop.slint',
            repo / 'userspace/gui/ui/src/main.rs',
            repo / 'userspace/gui/ui/src/apps.rs',
            repo / 'userspace/gui/libui/Cargo.toml',
            repo / 'userspace/gui/libui/lib.rs',
            repo / 'userspace/gui/libui/proto.rs',
            repo / 'userspace/gui/libui/draw.rs',
            repo / 'userspace/gui/libui/style.rs',
            repo / 'userspace/gui/libui/anim.rs',
            repo / 'userspace/gui/ui/components/background/background.slint',
            repo / 'userspace/gui/ui/components/window/window.slint',
            repo / 'userspace/gui/ui/components/dock/default/dock.slint',
            repo / 'userspace/gui/ui/components/overlay/overlay.slint',
            repo / 'userspace/gui/ui/components/cursor/cursor.slint',
            repo / 'userspace/lib/libvoid.rs',
            repo / 'userspace/lib/heap.rs',
        ),
        command: [
            'powershell', '-NoProfile', '-Command',
            'Set-Location \'' + (repo / 'userspace/gui/ui') + '\'; & \'' + cargo.full_path()
            + '\' build --release --target x86_64-unknown-none --target-dir \''
            + (repo / 'kernel/build/slint-target')
            + '\'; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; Copy-Item -Force \''
            + (repo / 'kernel/build/slint-target/x86_64-unknown-none/release/user-ui')
            + '\' \'' + (out / 'user-ui.elf') + '\'',
        ],
        build_by_default: true,
    )
endif

test('plan_parity', find_program(tool), args: ['parity', context], depends: elf, timeout: 60)
