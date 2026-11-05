#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]
mod state;
#[path = "../frame/validation.rs"]
mod frame;
static SERVICE: state::Service = state::Service::new();

// ====== xis_idle_return_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_idle_return_valid(frame: u64, top: u64, rsp: u64, cs: u64,
    flags: u64, ss: u64, diagnostic: u32) -> u32 {
    frame::idle_return_valid(frame,top,rsp,cs,flags,ss,diagnostic)
}

// ====== xis_configure ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_configure(count: u32) -> u32 { SERVICE.configure(count) }

// ====== xis_ready ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_ready(index: u32, caller: u32) -> u32 { SERVICE.ready(index, caller) }

// ====== xis_publish ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_publish(index: u32, caller: u32) -> u64 { SERVICE.publish(index, caller) }

// ====== xis_claim ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_claim(index: u32, caller: u32) -> u64 { SERVICE.claim(index, caller) }

// ====== xis_complete ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_complete(index: u32, caller: u32, generation: u64) -> u32 {
    SERVICE.complete(index, caller, generation)
}

// ====== xis_fail ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_fail(index: u32, caller: u32) -> u32 { SERVICE.fail(index, caller) }

// ====== xis_value ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_value(index: u32, field: u32) -> u64 { SERVICE.value(index, field) }

// ====== xis_publish_operation ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_publish_operation(index: u32, caller: u32, operation: u32) -> u64 {
    SERVICE.publish_operation(index, caller, operation)
}

// ====== xis_operation ======

#[unsafe(no_mangle)]
pub extern "C" fn xis_operation(index: u32, caller: u32, generation: u64) -> u32 {
    SERVICE.operation(index, caller, generation)
}
