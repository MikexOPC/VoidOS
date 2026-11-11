#![no_std]
#![no_main]
#![allow(unexpected_cfgs, dead_code)]

use core::arch::{asm, global_asm};
use core::panic::PanicInfo;

#[cfg(program_bridge_server)]
#[path = "../../../../bridge/api/rust/status/protocol.rs"]
mod bridge_protocol;
#[cfg(program_bridge_server)]
#[path = "../../../../bridge/service/rust/status/dispatch.rs"]
mod bridge_service;
#[cfg(any(program_bridge_client, program_bridge_server))]
#[path = "../../../bridge/rust/status/payload.rs"]
mod bridge_payload;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

#[unsafe(no_mangle)]
static MESSAGE: [u8; 13] = *b"hello, ring3\n";

#[unsafe(no_mangle)]
static mut COUNTER: u32 = 7;

#[unsafe(no_mangle)]
static mut SPACE: [u8; 8192] = [0; 8192];

// ====== syscall ======

fn syscall(number: u64, first: u64, second: u64) -> u64 {
    let result: u64;
    unsafe {
        asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") first,
            in("rsi") second,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result
}

// ====== exit ======

fn exit(code: u64) -> ! {
    syscall(0, code, 0);
    loop {}
}

// ====== touch ======

fn touch() -> u32 {
    unsafe { (&raw const COUNTER).read_volatile() }
}

// ====== program_main ======

#[cfg(program_hello)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    let preserved: u64;
    unsafe {
        asm!(
            "mov r12, {magic}",
            "mov eax, 99",
            "syscall",
            "cmp rax, -1",
            "jne 2f",
            "mov {out}, r12",
            "jmp 3f",
            "2:",
            "xor {out}, {out}",
            "3:",
            magic = in(reg) 0x5a5a1234c3c35678u64,
            out = out(reg) preserved,
            out("r12") _,
            out("rax") _,
            out("rcx") _,
            out("r11") _,
        );
    }
    if preserved != 0x5a5a1234c3c35678 {
        exit(9);
    }
    let written = syscall(1, MESSAGE.as_ptr() as u64, MESSAGE.len() as u64);
    exit(if written == 13 && touch() == 7 { 0 } else { 8 })
}

#[cfg(program_null)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    unsafe { asm!("xor eax, eax", "mov rax, [rax]", out("rax") _) };
    exit(1)
}

#[cfg(program_readonly)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    unsafe { asm!("mov byte ptr [{0}], 1", in(reg) MESSAGE.as_ptr()) };
    exit(1)
}

#[cfg(program_privileged)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    unsafe { asm!("cli") };
    exit(1)
}

#[cfg(program_kernel_jump)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    unsafe { asm!("jmp {0}", in(reg) 0xffffffff80001000u64) };
    exit(1)
}

#[cfg(program_bad_pointer)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    let kernel = syscall(1, 0xffffffff80000000, 8);
    let guard = syscall(1, 0x5fd000, 8);
    let long = syscall(1, MESSAGE.as_ptr() as u64, 257);
    let empty = syscall(1, MESSAGE.as_ptr() as u64, 0);
    let fine = kernel == u64::MAX - 1 && guard == u64::MAX - 1 && long == u64::MAX - 2 && empty == 0;
    exit(if fine { 7 } else { 6 })
}

#[cfg(program_bss)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    let space = (&raw mut SPACE).cast::<u8>();
    let seen = unsafe { space.add(8191).read_volatile() };
    unsafe { space.add(8191).write_volatile(0xa5) };
    exit(if seen == 0 { 0 } else { 5 })
}

#[cfg(program_irq)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    let difference: u64;
    unsafe {
        asm!(
            "mov r12, {magic}",
            "mov r13, r12",
            "mov r14, r12",
            "mov r15, r12",
            "movq xmm0, r12",
            "2:",
            "pause",
            "dec {count}",
            "jnz 2b",
            "movq {out}, xmm0",
            "xor {out}, r12",
            "xor r13, r12",
            "xor r14, r12",
            "xor r15, r12",
            "or {out}, r13",
            "or {out}, r14",
            "or {out}, r15",
            magic = in(reg) 0x1122334455667788u64,
            count = inout(reg) 0x200000u64 => _,
            out = out(reg) difference,
            out("r12") _,
            out("r13") _,
            out("r14") _,
            out("r15") _,
        );
    }
    exit(if difference == 0 { 0 } else { 4 })
}

#[cfg(program_spin)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    loop {
        unsafe { asm!("pause") };
    }
}

#[cfg(program_syscall_spin)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    loop {
        syscall(99, 0, 0);
    }
}

#[cfg(program_flags_spin)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    loop {
        unsafe { asm!("pushfq", "and qword ptr [rsp], -513", "popfq", "pause") };
    }
}

#[cfg(program_flags_ac)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    unsafe { asm!("pushfq", "or qword ptr [rsp], 0x40000", "popfq") };
    loop {
        unsafe { asm!("pause") };
    }
}

#[cfg(program_write_flood)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    static BLOCK: [u8; 256] = {
        let mut block = [b'x'; 256];
        block[255] = b'\n';
        block
    };
    touch();
    let mut accepted = 0u64;
    let refused = loop {
        let written = syscall(1, BLOCK.as_ptr() as u64, 256);
        if written != 256 {
            break written;
        }
        accepted += 1;
    };
    exit(if refused == u64::MAX - 3 && accepted == 16 { 0 } else { 3 })
}

#[cfg(program_spurious)]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    touch();
    let preserved: u64;
    unsafe {
        asm!(
            "mov r12, {magic}",
            "int 0xff",
            "mov {out}, r12",
            magic = in(reg) 0x0fedcba987654321u64,
            out = lateout(reg) preserved,
            out("r12") _,
        );
    }
    exit(if preserved == 0x0fedcba987654321 { 0 } else { 2 })
}

// ====== program_main ======

#[cfg(any(program_yield_a, program_yield_b))]
#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    #[cfg(program_yield_a)]
    const TAG: u64 = 0x1122334455667788;
    #[cfg(program_yield_b)]
    const TAG: u64 = 0x8877665544332211;
    if touch() != 7 {
        exit(1);
    }
    unsafe { (&raw mut COUNTER).write_volatile(TAG as u32) };
    for _ in 0..2 {
        let general: u64;
        let vector: u64;
        let status: u64;
        unsafe {
            asm!(
                "movq xmm0, r12",
                "syscall",
                "movq {vector}, xmm0",
                vector = lateout(reg) vector,
                inout("r12") TAG => general,
                inlateout("rax") 2u64 => status,
                lateout("rcx") _,
                lateout("r11") _,
            );
        }
        if general != TAG || vector != TAG || status != 0 || touch() != TAG as u32 {
            exit(2);
        }
    }
    exit(TAG)
}

// ====== ipc ======

#[cfg(any(program_ipc_client, program_ipc_server, program_bridge_client, program_bridge_server))]
fn ipc(number: u64, handle: u64, pointer: u64, length: u64) -> i64 {
    let result: u64;
    unsafe {
        asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") handle,
            in("rsi") pointer,
            in("rdx") length,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result as i64
}

// ====== program_main ======

#[cfg(program_ipc_client)]
#[unsafe(no_mangle)]
extern "C" fn program_main(handle: u64) -> ! {
    let mut bytes = [0u8, b'!'];
    let pointer = bytes.as_mut_ptr() as u64;
    if ipc(4, handle, pointer, 2) != -6 || ipc(3, handle ^ 1, pointer, 2) != -5 ||
        ipc(3, handle, 0x5fd000, 2) != -2 {
        exit(1);
    }
    for value in 1..=4 {
        bytes[0] = value;
        if ipc(3, handle, pointer, 2) != 2 {
            exit(2);
        }
    }
    if ipc(3, handle, pointer, 2) != -6 || syscall(2, 0, 0) != 0 {
        exit(3);
    }
    match ipc(4, handle, pointer, 2) {
        2 if bytes == *b"OK" => exit(0),
        -7 if ipc(3, handle, pointer, 2) == -7 => exit(77),
        _ => exit(4),
    }
}

// ====== program_main ======

#[cfg(program_ipc_server)]
#[unsafe(no_mangle)]
extern "C" fn program_main(handle: u64) -> ! {
    let mut bytes = [0u8; 2];
    let pointer = bytes.as_mut_ptr() as u64;
    if ipc(4, handle, pointer, 1) != -8 || ipc(4, handle, 0x400000, 2) != -2 {
        exit(1);
    }
    for value in 1..=4 {
        if ipc(4, handle, pointer, 2) != 2 || bytes != [value, b'!'] {
            exit(2);
        }
    }
    if ipc(4, handle, pointer, 2) != -6 ||
        ipc(3, handle, b"OK".as_ptr() as u64, 2) != 2 || syscall(2, 0, 0) != 0 {
        exit(3);
    }
    exit(if ipc(4, handle, pointer, 2) == -7 { 0 } else { 4 })
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
