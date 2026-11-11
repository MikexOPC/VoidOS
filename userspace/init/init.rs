#![no_std]
#![no_main]

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
static MESSAGE: [u8; 19] = *b"VoidOS init, ring3\n";

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

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    let written = syscall(1, MESSAGE.as_ptr() as u64, MESSAGE.len() as u64);
    syscall(0, if written == MESSAGE.len() as u64 { 0 } else { 1 }, 0);
    loop {}
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
