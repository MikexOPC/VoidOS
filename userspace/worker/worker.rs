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

// ====== worker_main ======

#[unsafe(no_mangle)]
extern "C" fn worker_main(handle: u64) -> ! {
    if handle == 0 {
        // Intel SDM Vol. 2B, UD2 (0F 0B): intentional #UD at CPL3 to prove fault isolation.
        unsafe { asm!("ud2", options(nostack)); }
    }
    let request: [u8; 2] = [1, 1];
    let mut response: [u8; 16] = [0; 16];
    if libvoid::ipc_send(handle, &request) != 2 {
        libvoid::exit(10);
    }
    if libvoid::ipc_wait_readable(handle) != 0 {
        libvoid::exit(11);
    }
    if libvoid::ipc_recv(handle, &mut response) != 2 {
        libvoid::exit(12);
    }
    if response[0] != 1 || response[1] != 0 {
        libvoid::exit(13);
    }
    let fd = libvoid::open(b"/HELLO.TXT", libvoid::O_RDONLY);
    if fd > 0 {
        let mut file_buf: [u8; 32] = [0; 32];
        let read_len = libvoid::read(fd as u64, &mut file_buf[..21]);
        if read_len != 21 || file_buf[0] != b'V' || file_buf[7] != b'F' {
            libvoid::exit(21);
        }
        if libvoid::close(fd as u64) != 0 {
            libvoid::exit(22);
        }
    }
    let written = libvoid::write_console(&MESSAGE);
    libvoid::exit(if written == MESSAGE.len() as i64 { 0 } else { 14 });
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
