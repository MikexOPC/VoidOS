#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

#[unsafe(no_mangle)]
static MESSAGE: [u8; 19] = *b"VoidOS init, ring3\n";
static CHILD_OK: [u8; 16] = *b"VoidOS child ok\n";
static SUPERVISOR_OK: [u8; 30] = *b"VoidOS init vfs supervisor ok\n";

// ====== run_sandboxed_child ======

fn run_sandboxed_child() -> ! {
    // Child was spawned with SPAWN flag revoked: spawning must be rejected.
    if libvoid::spawn(b"/bin/init.elf", None) >= 0 {
        libvoid::exit(50);
    }
    let fd = libvoid::open(b"/HELLO.TXT", libvoid::O_RDONLY);
    if fd <= 0 {
        libvoid::exit(51);
    }
    let mut buf = [0u8; 32];
    let n = libvoid::read(fd as u64, &mut buf[..21]);
    if n != 21 || buf[0] != b'V' || buf[7] != b'F' {
        libvoid::exit(52);
    }
    if libvoid::close(fd as u64) != 0 {
        libvoid::exit(53);
    }
    let written = libvoid::write_console(&CHILD_OK);
    libvoid::exit(if written == CHILD_OK.len() as i64 { 42 } else { 54 });
}

// ====== run_vfs_supervisor ======

fn run_vfs_supervisor() -> ! {
    // 1) Spawn a healthy child with narrowed budget (SPAWN revoked).
    let child_args = libvoid::SpawnArgs {
        initial_arg: 2,
        flags: 0,
        use_budget: 1,
        budget: libvoid::SpawnBudget {
            max_image_pages: 32,
            max_open_files: 4,
            cpu_budget_ms: 1000,
            flags: 0,
        },
    };
    let child_pid = libvoid::spawn(b"/bin/init.elf", Some(&child_args));
    if child_pid <= 0 {
        libvoid::exit(60);
    }
    let mut status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(child_pid as u64, Some(&mut status)) != 0 {
        libvoid::exit(61);
    }
    if status.exit_kind != libvoid::EXIT_KIND_NORMAL || status.exit_code != 42 {
        libvoid::exit(62);
    }

    // 2) Spawn a faulting child (/bin/worker.elf with arg=0 triggers CPL3 #UD).
    let fault_args = libvoid::SpawnArgs {
        initial_arg: 0,
        flags: 0,
        use_budget: 0,
        budget: libvoid::SpawnBudget::default(),
    };
    let fault_pid = libvoid::spawn(b"/bin/worker.elf", Some(&fault_args));
    if fault_pid <= 0 {
        libvoid::exit(63);
    }
    let mut fault_status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(fault_pid as u64, Some(&mut fault_status)) != 0 {
        libvoid::exit(64);
    }
    if fault_status.exit_kind != libvoid::EXIT_KIND_FAULT || fault_status.fault_vector != 6 {
        libvoid::exit(65);
    }

    // 3) Budget escalation must be rejected by the kernel.
    let bad_args = libvoid::SpawnArgs {
        initial_arg: 2,
        flags: 0,
        use_budget: 1,
        budget: libvoid::SpawnBudget {
            max_image_pages: 999,
            max_open_files: 4,
            cpu_budget_ms: 1000,
            flags: 0,
        },
    };
    if libvoid::spawn(b"/bin/init.elf", Some(&bad_args)) >= 0 {
        libvoid::exit(66);
    }

    let written = libvoid::write_console(&SUPERVISOR_OK);
    libvoid::exit(if written == SUPERVISOR_OK.len() as i64 { 0 } else { 67 });
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(mode: u64) -> ! {
    if mode == 1 {
        run_vfs_supervisor();
    }
    if mode == 2 {
        run_sandboxed_child();
    }
    let written = libvoid::write_console(&MESSAGE);
    libvoid::exit(if written == MESSAGE.len() as i64 { 0 } else { 1 });
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
