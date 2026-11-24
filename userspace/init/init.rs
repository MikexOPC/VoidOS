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
    // Child was spawned with SPAWN and DISPLAY flags revoked: both must be rejected.
    if libvoid::spawn(b"/bin/init.elf", None) >= 0 {
        libvoid::exit(50);
    }
    let mut acq = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut acq) >= 0 {
        libvoid::exit(55);
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

// ====== run_umip_probe ======

fn run_umip_probe() -> ! {
    // Intel SDM Vol. 3A §2.5 (CR4.UMIP = bit 11): SGDT at CPL > 0 must raise #GP(0) (vector 13).
    let mut gdtr = [0u8; 10];
    unsafe {
        core::arch::asm!("sgdt [{}]", in(reg) gdtr.as_mut_ptr(), options(nostack));
    }
    libvoid::exit(77);
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

    // 2b) Spawn a Ring 3 child that executes SGDT: with CR4.UMIP active it must fault with #GP (vector 13).
    let umip_args = libvoid::SpawnArgs {
        initial_arg: 3,
        flags: 0,
        use_budget: 0,
        budget: libvoid::SpawnBudget::default(),
    };
    let umip_pid = libvoid::spawn(b"/bin/init.elf", Some(&umip_args));
    if umip_pid <= 0 {
        libvoid::exit(68);
    }
    let mut umip_status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(umip_pid as u64, Some(&mut umip_status)) != 0 {
        libvoid::exit(69);
    }
    // With CR4.UMIP active, SGDT faults with #GP (vector 13); on CPUs/hypervisors without UMIP (e.g. WHPX), it exits 77.
    let umip_faulted = umip_status.exit_kind == libvoid::EXIT_KIND_FAULT && umip_status.fault_vector == 13;
    let umip_unsupported = umip_status.exit_kind == libvoid::EXIT_KIND_NORMAL && umip_status.exit_code == 77;
    if !umip_faulted && !umip_unsupported {
        libvoid::exit(70);
    }

    // 3) Budget escalation must be rejected by the kernel.
    let bad_args = libvoid::SpawnArgs {
        initial_arg: 2,
        flags: 0,
        use_budget: 1,
        budget: libvoid::SpawnBudget {
            max_image_pages: 9999,
            max_open_files: 4,
            cpu_budget_ms: 1000,
            flags: 0,
        },
    };
    if libvoid::spawn(b"/bin/init.elf", Some(&bad_args)) >= 0 {
        libvoid::exit(66);
    }

    // 4) GUI syscalls: display_info + Ring 3 RW+NX SHM create/map/access/unmap.
    let mut dinfo = libvoid::DisplayInfo::default();
    if libvoid::display_info(&mut dinfo) != 0 || dinfo.width == 0 || dinfo.height == 0 || dinfo.stride < dinfo.width {
        libvoid::exit(71);
    }
    let shm_id = libvoid::shm_create(2);
    if shm_id <= 0 {
        libvoid::exit(72);
    }
    let virt = libvoid::shm_map(shm_id as u64);
    if virt < 0x1000_0000 {
        libvoid::exit(73);
    }
    unsafe {
        let ptr = virt as *mut u64;
        core::ptr::write_volatile(ptr, 0xCAFE_BABE_1234_5678u64);
        core::ptr::write_volatile(ptr.add(512), 0xDEAD_BEEF_90AB_CDEFu64);
        if core::ptr::read_volatile(ptr) != 0xCAFE_BABE_1234_5678u64
            || core::ptr::read_volatile(ptr.add(512)) != 0xDEAD_BEEF_90AB_CDEFu64
        {
            libvoid::exit(74);
        }
    }
    if libvoid::shm_unmap(shm_id as u64) != 0 {
        libvoid::exit(75);
    }

    // 5) Launch /bin/ui.elf once in self-check mode (Slint render + terminal IPC + read-only SHM + clean release).
    let ui_args = libvoid::SpawnArgs {
        initial_arg: 1,
        flags: 0,
        use_budget: 1,
        budget: libvoid::SpawnBudget {
            max_image_pages: 1024,
            max_open_files: 8,
            cpu_budget_ms: 2000,
            flags: libvoid::BUDGET_FLAG_DISPLAY | libvoid::BUDGET_FLAG_SPAWN,
        },
    };
    let ui_pid = libvoid::spawn(b"/bin/ui.elf", Some(&ui_args));
    if ui_pid <= 0 {
        libvoid::exit(76);
    }
    let mut ui_status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(ui_pid as u64, Some(&mut ui_status)) != 0
        || ui_status.exit_kind != libvoid::EXIT_KIND_NORMAL
        || ui_status.exit_code != 0
    {
        libvoid::exit(77);
    }

    // 6) Launch /bin/terminal.elf and /bin/sysmon.elf concurrently (sandboxed, no DISPLAY flag)
    // to verify multi-app SHM coexistence, animated rendering, and zero-leak cleanup.
    let app_args = libvoid::SpawnArgs {
        initial_arg: 1,
        flags: 0,
        use_budget: 1,
        budget: libvoid::SpawnBudget {
            max_image_pages: 128,
            max_open_files: 8,
            cpu_budget_ms: 2000,
            flags: 0,
        },
    };
    let term_pid = libvoid::spawn(b"/bin/terminal.elf", Some(&app_args));
    let sysmon_pid = libvoid::spawn(b"/bin/sysmon.elf", Some(&app_args));
    if term_pid <= 0 || sysmon_pid <= 0 {
        libvoid::exit(78);
    }
    let mut term_status = libvoid::WaitStatus::default();
    let mut sysmon_status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(term_pid as u64, Some(&mut term_status)) != 0
        || term_status.exit_kind != libvoid::EXIT_KIND_NORMAL
        || term_status.exit_code != 0
    {
        libvoid::exit(79);
    }
    if libvoid::wait_pid(sysmon_pid as u64, Some(&mut sysmon_status)) != 0
        || sysmon_status.exit_kind != libvoid::EXIT_KIND_NORMAL
        || sysmon_status.exit_code != 0
    {
        libvoid::exit(80);
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
    if mode == 3 {
        run_umip_probe();
    }
    let written = libvoid::write_console(&MESSAGE);
    libvoid::exit(if written == MESSAGE.len() as i64 { 0 } else { 1 });
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
