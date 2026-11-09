#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "lea rdi, [rip + MESSAGE]",
    "mov esi, 13",
    "mov eax, 1",
    "syscall",
    "call fixture_touch",
    "mov edi, eax",
    "xor eax, eax",
    "syscall",
    "ud2",
);

#[unsafe(no_mangle)]
static MESSAGE: [u8; 13] = *b"hello, ring3\n";

#[unsafe(no_mangle)]
static mut COUNTER: u32 = 7;

#[unsafe(no_mangle)]
static mut SPACE: [u8; 8192] = [0; 8192];

// ====== fixture_touch ======

#[unsafe(no_mangle)]
extern "C" fn fixture_touch() -> u32 {
    unsafe {
        let space = (&raw mut SPACE).cast::<u8>();
        let counter = &raw mut COUNTER;
        space.add(8191).write_volatile(1);
        counter.read_volatile()
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
