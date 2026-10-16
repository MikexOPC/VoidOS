#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../packet/decode.rs"]
mod packet;

// ====== kernel_ps2_mouse_decode ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_mouse_decode(state: u32, byte: u32) -> u64 {
    packet::step(state, byte)
}
