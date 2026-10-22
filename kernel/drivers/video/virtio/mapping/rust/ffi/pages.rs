#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../pages/plan.rs"]
mod plan;

// ====== kernel_vgpu_pages ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_pages(ranges: plan::Ranges, bits: u32) -> plan::Plan {
    plan::build(ranges,bits)
}
