project(
    'voidos-kernel-build-host',
    'rust',
    meson_version: '>=1.12.1',
    default_options: ['rust_std=2024', 'warning_level=3', 'werror=true', 'optimization=2'],
)

unit = executable(
    'kernel_build_unit',
    'unit.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
)

test(
    'kernel_build_unit',
    unit,
    env: {'VOIDOS_REPO': meson.project_source_root() / '../../..'},
    timeout: 60,
)
