use super::{apply_on, core_lock, event_apply_on, kernel_scheduler_context_valid, reply,
    stop_apply_on, EventReply, Reply, STORAGE};
use super::state::Handle;

// ====== gateway_apply_cpu ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_apply_on(cpu: u32, operation: u32, index: u32,
    generation: u64) -> Reply {
    apply_on(cpu, operation, index, generation, 10)
}

// ====== gateway_event_apply_cpu ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_event_apply_on(cpu: u32, operation: u32, index: u32,
    generation: u64, event: u32, observed: u64) -> EventReply {
    event_apply_on(cpu, operation, index, generation, event, observed)
}

// ====== gateway_stop_apply_cpu ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_stop_apply_on(cpu: u32, operation: u32, index: u32,
    generation: u64) -> EventReply {
    stop_apply_on(cpu, operation, index, generation)
}

// ====== gateway_with_owner ======

fn with_owner(action: impl FnOnce(&mut super::Owner) -> Result<u32, super::Error>) -> (u32, u32) {
    let Some(_guard) = core_lock::enter() else { return (9, 0); };
    if unsafe { kernel_scheduler_context_valid() } != 1 { return (10, 0); }
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { return (11, 0); };
    match action(owner) {
        Ok(value) => (0, value),
        Err(error) => (error as u32, 0),
    }
}

// ====== gateway_configure ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_configure(cpus: u32, slice: u32) -> u32 {
    with_owner(|owner| owner.configure(cpus, slice).map(|()| 0)).0
}

// ====== gateway_affinity ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_affinity_on(cpu: u32, index: u32, generation: u64, mask: u64) -> u32 {
    with_owner(|owner| owner.set_affinity_on(cpu, Handle { index, generation }, mask).map(|()| 0)).0
}

// ====== gateway_tick ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_tick_on(cpu: u32) -> Reply {
    let (status, preempt) = with_owner(|owner| owner.tick_on(cpu).map(u32::from));
    let mut output = reply(status);
    if status == 0 { output.state = preempt; }
    output
}
