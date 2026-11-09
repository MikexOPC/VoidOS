project(
    'voidos-process-elf-host',
    'rust',
    meson_version: '>=1.12.1',
    default_options: ['rust_std=2024', 'warning_level=3', 'werror=true', 'optimization=2'],
)

repo = '../../../..'
rustc = find_program('rustc')
parser = files(repo / 'kernel/process/rust/elf/parser.rs')

fixture = custom_target(
    'hello_elf',
    input: files('fixture/hello.rs'),
    output: 'hello.elf',
    depfile: 'hello.d',
    depend_files: files('fixture/link.ld'),
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=bin',
        '--target', 'x86_64-unknown-none', '-C', 'panic=abort', '-C', 'opt-level=s',
        '-C', 'relocation-model=static', '-C', 'link-arg=-T' + meson.current_source_dir() / 'fixture/link.ld',
        '-C', 'link-arg=-zmax-page-size=4096', '-C', 'link-arg=--build-id=none',
        '--emit=link=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

foreach optimization : ['0', '2']
    name = 'elf_parser_o' + optimization
    binary = executable(
        name,
        'tests.rs',
        extra_files: parser,
        rust_args: ['--test', '-C', 'overflow-checks=yes'],
        override_options: ['optimization=' + optimization],
    )
    test(name, binary, depends: fixture, env: {'VOIDOS_ELF_FIXTURE': fixture.full_path()}, timeout: 120)
endforeach

foreach optimization : ['0', '2']
    name = 'process_policy_o' + optimization
    binary = executable(
        name,
        repo / 'kernel/process/rust/lib.rs',
        rust_args: ['--test', '-C', 'overflow-checks=yes'],
        override_options: ['optimization=' + optimization],
    )
    test(name, binary, timeout: 120)
endforeach
