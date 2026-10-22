#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../policy/features.rs"]
mod features;

// ====== kernel_vgpu_features ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_features(offered: u64) -> u64 {
    features::features(offered)
}

// ====== kernel_vgpu_register_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_register_valid(offset: u32, width: u32, write: u32) -> u32 {
    features::register_valid(offset,width,write)
}
