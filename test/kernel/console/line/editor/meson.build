project(
    'voidos-line-editor-host',
    'c',
    'rust',
    meson_version: '>=1.12.1',
    default_options: [
        'c_std=c11',
        'rust_std=2024',
        'warning_level=3',
        'werror=true',
        'optimization=2',
        'b_ndebug=false',
        'b_vscrt=md',
        'b_sanitize=address',
    ],
)

if host_machine.system() != 'windows' or host_machine.cpu_family() != 'x86_64' or meson.get_compiler('c').get_id() != 'msvc'
    error('The line editor pilot requires x64 Windows and MSVC.')
endif

repo = '../../../../..'
rustc = find_program('rustc')
editor = files(repo / 'kernel/console/line/rust/editor/plan.rs')

foreach optimization : ['0', '2']
    name = 'line_rust_o' + optimization
    binary = executable(
        name,
        editor,
        rust_args: ['--test', '-C', 'overflow-checks=yes'],
        override_options: ['optimization=' + optimization, 'b_sanitize=none'],
    )
    test(name, binary)
endforeach

gateway = custom_target(
    'line_gateway',
    input: files(repo / 'kernel/input/integration/rust/ffi/ps2.rs'),
    output: 'line_gateway.obj',
    depfile: 'line_gateway.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

abi = executable(
    'line_abi',
    'abi_test.c',
    repo / 'kernel/console/line/line.c',
    gateway,
    include_directories: include_directories(repo),
)
test('line_abi', abi)
