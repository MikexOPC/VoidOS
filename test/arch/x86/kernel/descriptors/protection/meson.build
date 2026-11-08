project(
    'voidos-user-protection-host',
    'rust',
    meson_version: '>=1.12.1',
    default_options: ['rust_std=2024', 'warning_level=3', 'werror=true'],
)

policy = files('../../../../../../arch/x86/kernel/descriptors/rust/protection/policy.rs')

foreach optimization : ['0', '2']
    name = 'user_protection_o' + optimization
    binary = executable(
        name,
        policy,
        rust_args: ['--test', '-C', 'overflow-checks=yes'],
        override_options: ['optimization=' + optimization],
    )
    test(name, binary)
endforeach
