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

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main() -> ! {
    let written = libvoid::write_console(&MESSAGE);
    libvoid::exit(if written == MESSAGE.len() as i64 { 0 } else { 1 });
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
