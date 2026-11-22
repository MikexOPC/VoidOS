#![no_std]
#![no_main]

use core::arch::global_asm;
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

// ====== bridge_main ======

#[unsafe(no_mangle)]
extern "C" fn bridge_main(handle: u64) -> ! {
    let mut request = [0u8; 128];
    loop {
        match libvoid::ipc_wait_readable(handle) {
            0 => {}
            libvoid::ERR_PEER_CLOSED => libvoid::exit(0),
            _ => libvoid::exit(1),
        }
        let length = libvoid::ipc_recv(handle, &mut request);
        let Ok(length) = usize::try_from(length) else { libvoid::exit(2); };
        let Some(frame) = request.get(..length) else { libvoid::exit(2); };
        #[cfg(bridge_test_fault)]
        // Intel SDM vol. 2B, UD2 (0F 0B): raise #UD from the test service at CPL3.
        unsafe { core::arch::asm!("ud2", options(nostack)); }
        let response = bridge_service::reply(frame).encode();
        if libvoid::ipc_send(handle, &response) != response.len() as i64 {
            libvoid::exit(3);
        }
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    libvoid::exit(4);
}
