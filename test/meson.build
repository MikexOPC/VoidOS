project(
    'voidos-host-tests',
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
    error('VoidOS host tests require x64 Windows and MSVC.')
endif

fs = import('fs')
repo = '..'
inc = include_directories(repo)
rustc = find_program('rustc')
runtime = environment()
runtime.prepend('PATH', fs.parent(find_program('cl').full_path()))

# ====== clock ======

clock_test = executable(
    'clock_host',
    repo / 'kernel/time/clock.c',
    'kernel/time/clock_test.c',
    include_directories: inc,
)
test('clock', clock_test, env: runtime)

# ====== sync ======

sync_rust = executable(
    'sync_atomics_rust',
    'kernel/sync/rust/atomics/operations_test.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('sync_atomics_rust', sync_rust)

sync_atomics_obj = custom_target(
    'sync_atomics_obj',
    input: files(repo / 'kernel/sync/rust/atomics/operations.rs'),
    output: 'sync_atomics.obj',
    depfile: 'sync_atomics.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

sync_host = executable(
    'sync_host',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    repo / 'arch/x86/lib/synchronization/context.c',
    'kernel/sync/context/provider.c',
    'kernel/sync/backend.c',
    'kernel/sync/cases.c',
    'kernel/sync/threads.c',
    'kernel/sync/main.c',
    sync_atomics_obj,
    include_directories: inc,
)
test('sync_core', sync_host, env: runtime)
foreach threads : ['2', '4', '8']
    test('sync_threads_' + threads, sync_host, args: ['threads', threads], env: runtime)
endforeach

# ====== scheduler ======

scheduler_owner_rust = executable(
    'scheduler_owner_rust',
    repo / 'kernel/scheduler/rust/state/owner.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('scheduler_owner_rust', scheduler_owner_rust)

scheduler_bindings_rust = executable(
    'scheduler_bindings_rust',
    repo / 'kernel/scheduler/rust/irq/bindings.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('scheduler_bindings_rust', scheduler_bindings_rust)

scheduler_payload_rust = executable(
    'scheduler_payload_rust',
    repo / 'kernel/scheduler/rust/payload/channel.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('scheduler_payload_rust', scheduler_payload_rust)

scheduler_preempt = executable(
    'scheduler_preempt',
    repo / 'kernel/scheduler/preempt.c',
    'kernel/scheduler/preempt_test.c',
    include_directories: inc,
)
test('scheduler_preempt', scheduler_preempt, env: runtime)

# ====== console line editor ======

line_editor_rust = executable(
    'line_editor_rust',
    repo / 'kernel/console/line/rust/editor/plan.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('line_editor_rust', line_editor_rust)

line_gateway_obj = custom_target(
    'line_gateway_obj',
    input: files(repo / 'kernel/input/integration/rust/ffi/ps2.rs'),
    output: 'line_gateway.obj',
    depfile: 'line_gateway.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

line_abi = executable(
    'line_abi',
    'kernel/console/line/editor/abi_test.c',
    repo / 'kernel/console/line/line.c',
    line_gateway_obj,
    include_directories: inc,
)
test('line_abi', line_abi, env: runtime)

# ====== process policy & elf parser ======

hello_elf = custom_target(
    'hello_elf',
    input: files('kernel/process/elf/fixture/hello.rs'),
    output: 'hello.elf',
    depfile: 'hello.d',
    depend_files: files('kernel/process/elf/fixture/link.ld'),
    build_by_default: true,
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=bin',
        '--target', 'x86_64-unknown-none', '-C', 'panic=abort', '-C', 'opt-level=s',
        '-C', 'relocation-model=static',
        '-C', 'link-arg=-T' + meson.current_source_dir() / 'kernel/process/elf/fixture/link.ld',
        '-C', 'link-arg=-zmax-page-size=4096', '-C', 'link-arg=--build-id=none',
        '--emit=link=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

elf_parser_rust = executable(
    'elf_parser_rust',
    'kernel/process/elf/tests.rs',
    extra_files: files(repo / 'kernel/process/rust/elf/parser.rs'),
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('elf_parser_rust', elf_parser_rust, depends: hello_elf, env: {'VOIDOS_ELF_FIXTURE': hello_elf.full_path()}, timeout: 120)

process_policy_rust = executable(
    'process_policy_rust',
    repo / 'kernel/process/rust/lib.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('process_policy_rust', process_policy_rust, timeout: 120)

process_ipc_obj = custom_target(
    'process_ipc_obj',
    input: files(repo / 'kernel/process/rust/ipc/channel.rs'),
    output: 'process_ipc.obj',
    depfile: 'process_ipc.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

process_table_host = executable(
    'process_table_host',
    'kernel/process/table_test.c',
    repo / 'kernel/process/table.c',
    repo / 'kernel/mm/heap/heap.c',
    repo / 'kernel/mm/heap/core/fill.c',
    repo / 'kernel/mm/heap/core/guard.c',
    repo / 'kernel/mm/heap/core/release.c',
    repo / 'kernel/mm/heap/core/reserve.c',
    repo / 'kernel/mm/heap/core/state.c',
    repo / 'lib/containers/bitmap.c',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    sync_atomics_obj,
    process_ipc_obj,
    include_directories: inc,
)
test('process_table_host', process_table_host, env: runtime)

# ====== x86 descriptors & watchdog/log integration ======

user_protection_rust = executable(
    'user_protection_rust',
    repo / 'arch/x86/kernel/descriptors/rust/protection/policy.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('user_protection_rust', user_protection_rust)

watchdog_integration = executable(
    'watchdog_integration',
    'arch/x86/kernel/timer/hpet/watchdog/integration_test.c',
    repo / 'arch/x86/kernel/timer/hpet/watchdog/integration/kernel.c',
    include_directories: inc,
    c_args: ['-DKERNEL_USER_PROCESS', '-DKERNEL_PS2_INPUT'],
)
test('watchdog_integration', watchdog_integration, env: runtime)

log_boundary = executable(
    'log_boundary',
    repo / 'kernel/diagnostics/log.c',
    'kernel/diagnostics/log_test.c',
    include_directories: inc,
    c_args: ['-DKERNEL_PS2_INPUT'],
)
test('log_boundary', log_boundary, env: runtime)

# ====== physical allocator ======

physical_placement_obj = custom_target(
    'physical_placement_obj',
    input: files(repo / 'kernel/mm/physical/rust/alignment/placement.rs'),
    output: 'physical_placement.obj',
    depfile: 'physical_placement.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

mm_regions_obj = custom_target(
    'mm_regions_obj',
    input: files(repo / 'kernel/mm/rust/regions/validation.rs'),
    output: 'mm_regions.obj',
    depfile: 'mm_regions.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

physical_allocator_host = executable(
    'physical_allocator_host',
    'kernel/mm/physical/allocator_test.c',
    'kernel/mm/physical/synchronization/backend.c',
    repo / 'kernel/mm/physical/allocator.c',
    repo / 'kernel/mm/physical/allocation/guard.c',
    repo / 'kernel/mm/physical/allocation/state.c',
    repo / 'kernel/mm/physical/allocation/queries.c',
    repo / 'kernel/mm/inventory.c',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    sync_atomics_obj,
    physical_placement_obj,
    mm_regions_obj,
    include_directories: inc,
    c_args: ['-DVOIDOS_HOST_MAIN'],
)
test('physical_allocator_host', physical_allocator_host, env: runtime)

# ====== kernel heap ======

heap_host = executable(
    'heap_host',
    'kernel/mm/heap/heap_test.c',
    'kernel/mm/physical/synchronization/backend.c',
    repo / 'kernel/mm/heap/heap.c',
    repo / 'kernel/mm/heap/core/fill.c',
    repo / 'kernel/mm/heap/core/guard.c',
    repo / 'kernel/mm/heap/core/release.c',
    repo / 'kernel/mm/heap/core/reserve.c',
    repo / 'kernel/mm/heap/core/state.c',
    repo / 'lib/containers/bitmap.c',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    sync_atomics_obj,
    include_directories: inc,
)
test('heap_host', heap_host, env: runtime, timeout: 120)

# ====== storage block cache ======

block_cache_host = executable(
    'block_cache_host',
    'kernel/storage/cache_test.c',
    'kernel/mm/physical/synchronization/backend.c',
    repo / 'kernel/storage/cache.c',
    repo / 'kernel/mm/heap/heap.c',
    repo / 'kernel/mm/heap/core/fill.c',
    repo / 'kernel/mm/heap/core/guard.c',
    repo / 'kernel/mm/heap/core/release.c',
    repo / 'kernel/mm/heap/core/reserve.c',
    repo / 'kernel/mm/heap/core/state.c',
    repo / 'lib/containers/bitmap.c',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    sync_atomics_obj,
    include_directories: inc,
)
test('block_cache_host', block_cache_host, env: runtime)

virtio_blk_protocol_rust = executable(
    'virtio_blk_protocol_rust',
    repo / 'kernel/drivers/storage/virtio_blk/rust/protocol.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('virtio_blk_protocol_rust', virtio_blk_protocol_rust)

ahci_protocol_rust = executable(
    'ahci_protocol_rust',
    repo / 'kernel/drivers/storage/ahci/rust/protocol.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('ahci_protocol_rust', ahci_protocol_rust)

# ====== VFS & FAT filesystem ======

vfs_fat_rust = executable(
    'vfs_fat_rust',
    repo / 'kernel/vfs/rust/fat.rs',
    rust_args: ['--test', '-C', 'overflow-checks=yes'],
    override_options: ['b_sanitize=none'],
)
test('vfs_fat_rust', vfs_fat_rust)

vfs_fat_obj = custom_target(
    'vfs_fat_obj',
    input: files(repo / 'kernel/vfs/rust/fat.rs'),
    output: 'vfs_fat.obj',
    depfile: 'vfs_fat.d',
    command: [
        rustc, '--edition=2024', '-D', 'warnings', '--crate-type=lib',
        '--crate-name', 'vfs_fat',
        '-C', 'panic=abort', '-C', 'opt-level=2',
        '--emit=obj=@OUTPUT@,dep-info=@DEPFILE@', '@INPUT@',
    ],
)

vfs_fat_host = executable(
    'vfs_fat_host',
    'kernel/vfs/vfs_test.c',
    'kernel/mm/physical/synchronization/backend.c',
    repo / 'kernel/vfs/vfs.c',
    repo / 'kernel/storage/cache.c',
    repo / 'kernel/mm/heap/heap.c',
    repo / 'kernel/mm/heap/core/fill.c',
    repo / 'kernel/mm/heap/core/guard.c',
    repo / 'kernel/mm/heap/core/release.c',
    repo / 'kernel/mm/heap/core/reserve.c',
    repo / 'kernel/mm/heap/core/state.c',
    repo / 'lib/containers/bitmap.c',
    repo / 'kernel/sync/lock.c',
    repo / 'kernel/sync/acquire.c',
    repo / 'kernel/time/clock.c',
    sync_atomics_obj,
    vfs_fat_obj,
    include_directories: inc,
)
test('vfs_fat_host', vfs_fat_host, env: runtime)
