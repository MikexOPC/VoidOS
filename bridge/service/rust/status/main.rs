#![no_std]
#![no_main]

use core::arch::{asm, global_asm};
use core::panic::PanicInfo;

#[path = "../../../api/rust/status/protocol.rs"]
mod bridge_protocol;
#[path = "dispatch.rs"]
mod bridge_service;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call bridge_main",
    "ud2",
);

// ====== syscall ======

fn syscall(number: u64, handle: u64, pointer: u64, length: u64) -> i64 {
    let result: i64;
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
    result
}

// ====== exit ======

fn exit(code: u64) -> ! {
    syscall(0, code, 0, 0);
    loop { core::hint::spin_loop(); }
}

// ====== bridge_main ======

#[unsafe(no_mangle)]
extern "C" fn bridge_main(handle: u64) -> ! {
    let mut request = [0u8; 128];
    loop {
        match syscall(5, handle, 0, 0) {
            0 => {}
            -7 => exit(0),
            _ => exit(1),
        }
        let length = syscall(4, handle, request.as_mut_ptr() as u64, request.len() as u64);
        let Ok(length) = usize::try_from(length) else { exit(2); };
        let Some(frame) = request.get(..length) else { exit(2); };
        #[cfg(bridge_test_fault)]
        // Intel SDM vol. 2B, UD2 (0F 0B): raise #UD from the test service at CPL3.
        unsafe { asm!("ud2", options(nostack)); }
        let response = bridge_service::reply(frame).encode();
        if syscall(3, handle, response.as_ptr() as u64, response.len() as u64) != response.len() as i64 {
            exit(3);
        }
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_: &PanicInfo) -> ! { exit(4) }
