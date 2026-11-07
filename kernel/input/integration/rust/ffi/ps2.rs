#![no_std]

// Integration-only gateway: the PS/2 driver does not depend on the shell.
#[path = "../../../../drivers/input/ps2/rust/decoder/set2.rs"]
pub mod set2;
#[path = "../../../keyboard/rust/text/italian.rs"]
pub mod italian;
#[path = "../../../keyboard/rust/text/utf8.rs"]
pub mod utf8;
#[path = "../../../../console/line/rust/editor/plan.rs"]
pub mod line;
#[path = "../../../../shell/rust/command/command.rs"]
pub mod command;
#[path = "../../../../shell/rust/command/queue.rs"]
pub mod queue;

// ====== kernel_ps2_decode ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_decode(state: u32, byte: u32) -> u64 {
    set2::step(state, byte)
}

// ====== kernel_ps2_text ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_text(state: u32, event: u32) -> u64 {
    italian::step(state, event)
}

// ====== kernel_ps2_utf8 ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_utf8(scalar: u32) -> u64 {
    utf8::encode(scalar)
}

// ====== kernel_ps2_line_plan ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_line_plan(length: u32, tail: u32, input: u32) -> u64 {
    line::step(length, tail, input)
}

// ====== kernel_ps2_shell_classify ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_shell_classify(word: u64, length: u32, arguments: u32) -> u32 {
    command::classify(word, length, arguments)
}

// ====== kernel_ps2_shell_count ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_shell_count(word: u64, length: u32) -> u64 {
    command::count(word, length)
}

// ====== kernel_ps2_queue_plan ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_ps2_queue_plan(state: u32, operation: u32) -> u64 {
    queue::apply(state, operation)
}
