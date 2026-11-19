project('voidos-watchdog-integration', 'c', meson_version: '>=1.12.1',
    default_options: ['c_std=c11', 'warning_level=3', 'werror=true', 'b_sanitize=address'])

fs = import('fs')
repo = '../../../../../../..'
runtime = environment()
if meson.get_compiler('c').get_id() == 'msvc'
    runtime.prepend('PATH', fs.parent(find_program('cl').full_path()))
endif

integration = executable('watchdog_integration', 'integration_test.c',
    repo / 'arch/x86/kernel/timer/hpet/watchdog/integration/kernel.c',
    include_directories: include_directories(repo),
    c_args: ['-DKERNEL_USER_PROCESS', '-DKERNEL_PS2_INPUT'])
test('watchdog_integration', integration, env: runtime)

log_boundary = executable('log_boundary', repo / 'kernel/diagnostics/log.c',
    repo / 'test/kernel/diagnostics/log_test.c',
    include_directories: include_directories(repo), c_args: ['-DKERNEL_PS2_INPUT'])
test('log_boundary', log_boundary, env: runtime)
