project(
    'voidos-qemu-runner',
    'rust',
    meson_version: '>=1.12.1',
    default_options: ['rust_std=2024', 'warning_level=3', 'werror=true', 'optimization=2'],
)

sources = '../../../scripts/tooling/qemu-runner/rust'

core = static_library(
    'qemu_runner_core',
    sources / 'runner/lib.rs',
    rust_abi: 'rust',
    rust_args: ['-C', 'overflow-checks=yes'],
)

runner = executable(
    'qemu_runner',
    sources / 'runner/main.rs',
    link_with: core,
    rust_args: ['-C', 'overflow-checks=yes'],
)

fake_qemu = executable(
    'fake_qemu',
    'fake_qemu.rs',
    rust_args: ['-C', 'overflow-checks=yes'],
)

unit = executable(
    'qemu_runner_unit',
    'unit.rs',
    link_with: core,
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
)

process = executable(
    'qemu_runner_process',
    'process.rs',
    link_with: core,
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
)

test('qemu_runner_unit', unit, timeout: 60)

test(
    'qemu_runner_process',
    process,
    args: ['--test-threads=1'],
    depends: [runner, fake_qemu],
    env: {
        'VOIDOS_RUNNER': runner.full_path(),
        'VOIDOS_FAKE_QEMU': fake_qemu.full_path(),
        'VOIDOS_SCRATCH': meson.current_build_dir() / 'process-scratch',
    },
    timeout: 180,
)
