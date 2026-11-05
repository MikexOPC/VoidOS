#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]
mod state;
#[path = "../frame/validation.rs"]
mod frame;
#[path = "../../../../../scheduler/admission/rust/state/owner.rs"]
mod admission;
static SERVICE: state::Service = state::Service::new();
static ADMISSION: admission::Admission = admission::Admission::new();

// ====== xad_configure ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_configure(count: u32, group: u64, caller: u32) -> u32 {
    ADMISSION.configure(count,group,caller)
}

// ====== xad_offer ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_offer(cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
    ADMISSION.offer(cpu,caller,token,group)
}

// ====== xad_claim ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_claim(cpu: u32, caller: u32, group: u64) -> u64 {
    ADMISSION.claim(cpu,caller,group)
}

// ====== xad_admit ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_admit(cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
    ADMISSION.admit(cpu,caller,token,group)
}

// ====== xad_fail ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_fail(cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
    ADMISSION.fail(cpu,caller,token,group)
}

// ====== xad_value ======

#[unsafe(no_mangle)]
pub extern "C" fn xad_value(cpu: u32, group: u64) -> u64 { ADMISSION.value(cpu,group) }

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
