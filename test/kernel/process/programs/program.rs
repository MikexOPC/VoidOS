#![no_std]
#![no_main]
#![allow(unexpected_cfgs)]

use core::arch::{asm, global_asm};
use core::panic::PanicInfo;

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
    let mut remaining = 0x800000u64;
    while unsafe { (&raw const remaining).read_volatile() } != 0 {
        remaining -= 1;
        unsafe { asm!("pause") };
    }
    exit(0)
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
