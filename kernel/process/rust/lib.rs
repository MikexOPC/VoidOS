#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

#[path = "elf/parser.rs"]
pub mod parser;
#[path = "policy/plan.rs"]
pub mod plan;
#[path = "policy/rules.rs"]
pub mod rules;
#[path = "ffi/exports.rs"]
pub mod exports;
#[path = "ipc/channel.rs"]
pub mod ipc;
