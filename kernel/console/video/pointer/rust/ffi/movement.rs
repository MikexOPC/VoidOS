#![no_std]
#[path = "../movement/plan.rs"]
mod movement;

// ====== kernel_pointer_plan ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_pointer_plan(width: u32, height: u32, x: u32, y: u32, dx: i32, dy: i32) -> u64 {
    movement::position(width, height, x, y, dx, dy)
}
