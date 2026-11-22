#![no_std]
#![no_main]

use core::arch::{asm, global_asm};
use core::panic::PanicInfo;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call worker_main",
    "ud2",
);

#[unsafe(no_mangle)]
static MESSAGE: [u8; 21] = *b"VoidOS ipc client ok\n";

// ====== syscall ======

fn syscall(number: u64, first: u64, second: u64, third: u64) -> i64 {
    let result: i64;
    unsafe {
        asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") first,
            in("rsi") second,
            in("rdx") third,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result
}

// ====== exit ======

fn exit(code: u64) -> ! {
    syscall(0, code, 0, 0);
    loop {
        core::hint::spin_loop();
    }
}

// ====== worker_main ======

#[unsafe(no_mangle)]
extern "C" fn worker_main(handle: u64) -> ! {
    if handle == 0 {
        // Intel SDM Vol. 2B, UD2 (0F 0B): intentional #UD at CPL3 to prove fault isolation.
        unsafe { asm!("ud2", options(nostack)); }
    }
    let request: [u8; 2] = [1, 1];
    let mut response: [u8; 16] = [0; 16];
    if syscall(3, handle, request.as_ptr() as u64, request.len() as u64) != 2 {
        exit(10);
    }
    if syscall(5, handle, 0, 0) != 0 {
        exit(11);
    }
    if syscall(4, handle, response.as_mut_ptr() as u64, response.len() as u64) != 2 {
        exit(12);
    }
    if response[0] != 1 || response[1] != 0 {
        exit(13);
    }
    let written = syscall(1, MESSAGE.as_ptr() as u64, MESSAGE.len() as u64, 0);
    exit(if written == MESSAGE.len() as i64 { 0 } else { 14 });
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    exit(99);
}
