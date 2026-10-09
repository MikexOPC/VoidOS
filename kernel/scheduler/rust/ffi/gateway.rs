#![no_std]

#[path = "../state/owner.rs"]
mod state;
#[path = "../irq/pending.rs"]
mod pending;
mod binding;
mod payload;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};
use state::{Error, Handle, Owner, NONE};

#[repr(C)]
pub struct Reply {
    status: u32,
    index: u32,
    generation: u64,
    state: u32,
    ready: u32,
    events: u64,
}

const _: () = {
    assert!(core::mem::size_of::<Reply>() == 32);
    assert!(core::mem::align_of::<Reply>() == 8);
    assert!(core::mem::offset_of!(Reply, status) == 0);
    assert!(core::mem::offset_of!(Reply, index) == 4);
    assert!(core::mem::offset_of!(Reply, generation) == 8);
    assert!(core::mem::offset_of!(Reply, state) == 16);
    assert!(core::mem::offset_of!(Reply, ready) == 20);
    assert!(core::mem::offset_of!(Reply, events) == 24);
};

struct Storage(UnsafeCell<Option<Owner>>);

// SAFETY: every access to Storage is under the exclusive ENTERED guard below.
// No borrow escapes apply, including errors. IRQ/NMI/SMP use is unsupported.
unsafe impl Sync for Storage {}
static STORAGE: Storage = Storage(UnsafeCell::new(None));
static ENTERED: AtomicBool = AtomicBool::new(false);
static PENDING: pending::Pending = pending::Pending::new();

unsafe extern "C" {
    // Trusted backend must verify BSP/IF=0/DF=0 and dispatcher, outside IRQ/NMI.
    // Host tests mock this predicate; no CPU verification is claimed by Rust.
    fn kernel_scheduler_context_valid() -> u32;
    fn kernel_scheduler_binding_context_valid() -> u32;
    fn kernel_scheduler_payload_context_valid(consumer: u32, generation: u64) -> u32;
    fn kernel_scheduler_fail_stop() -> !;
}

struct Guard;

#[repr(C)]
pub struct EventReply { reply: Reply, epoch: u64, outcome: u32, woken: u32 }
const _: () = {
    assert!(core::mem::size_of::<EventReply>() == 48);
    assert!(core::mem::align_of::<EventReply>() == 8);
    assert!(core::mem::offset_of!(EventReply, epoch) == 32);
    assert!(core::mem::offset_of!(EventReply, outcome) == 40);
    assert!(core::mem::offset_of!(EventReply, woken) == 44);
};

// ====== gateway_release_guard ======

impl Drop for Guard {
    fn drop(&mut self) { ENTERED.store(false, Ordering::Release); }
}

// ====== gateway_reply ======

fn reply(status: u32) -> Reply {
    Reply { status, index: NONE, generation: 0, state: NONE, ready: 0, events: 0 }
}

// ====== gateway_apply ======

// Private C ABI. Native resources must be verified before PUBLISH/ABORT/COLLECT.
// Struct return uses the target ABI; caller must provide valid return storage.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_apply(operation: u32, index: u32, generation: u64) -> Reply {
    if ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
        return reply(9); // BUSY: no owner borrow, no backend callback.
    }
    let _guard = Guard;
    // SAFETY: the linked backend has the stated C ABI and returns a normalized bit.
    // Callback occurs BEFORE creating any Rust reference to STORAGE.
    if unsafe { kernel_scheduler_context_valid() } != 1 { return reply(10); }
    if operation > 9 { return reply(12); }
    if matches!(operation, 0 | 1 | 4 | 9) && (index != NONE || generation != 0) {
        return reply(12);
    }
    // SAFETY: ENTERED exclusively owns Storage; no callbacks occur during borrow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    if operation == 0 {
        if storage.is_some() { return reply(13); }
        *storage = Some(Owner::new());
        return reply(0);
    }
    let Some(owner) = storage.as_mut() else { return reply(11); };
    let handle = Handle { index, generation };
    let result: Result<Handle, Error> = match operation {
        1 => owner.reserve(),
        2 => owner.publish(handle).map(|()| handle),
        3 => owner.abort(handle).map(|()| handle),
        4 => owner.select(),
        5 => owner.yield_task(handle).map(|()| handle),
        6 => owner.exit(handle).map(|()| handle),
        7 => owner.collect(handle).map(|()| handle),
        8 => owner.state(handle).map(|_| handle),
        9 => owner.current(),
        _ => return reply(12),
    };
    let mut output = match result {
        Ok(handle) => {
            let mut output = reply(0);
            output.index = handle.index;
            output.generation = handle.generation;
            output.state = match owner.state(handle) {
                Ok(state) => state as u32,
                Err(_) => return reply(1),
            };
            output
        }
        Err(error) => reply(error as u32),
    };
    if let Ok((ready, events)) = owner.snapshot() {
        output.ready = ready;
        output.events = events;
    }
    output
}

// ====== gateway_event_apply ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_event_apply(operation: u32, index: u32, generation: u64,
    event: u32, observed: u64) -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: u32::MAX, woken: 0 };
    if ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
        output.reply = reply(9); return output;
    }
    let _guard = Guard;
    // SAFETY: trusted ABI callback, before borrowing the exclusively owned storage.
    if unsafe { kernel_scheduler_context_valid() } != 1 {
        output.reply = reply(10); return output;
    }
    if operation > 2 || (operation != 1 && observed != 0) {
        output.reply = reply(12); return output;
    }
    // SAFETY: same exclusive guard as apply; no references escape or callbacks follow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    let handle = Handle { index, generation };
    let result: Result<state::EventResult, Error> = match operation {
        0 => owner.observe(handle, event),
        1 => owner.wait(handle, event, observed),
        2 => owner.signal(handle, event),
        _ => unreachable!(),
    };
    match result {
        Ok(result) => {
            let Ok(state) = owner.state(handle) else { output.reply = reply(1); return output; };
            output.reply.index = index; output.reply.generation = generation;
            output.reply.state = state as u32;
            output.epoch = result.epoch; output.outcome = result.outcome; output.woken = result.woken;
        }
        Err(error) => output.reply = reply(error as u32),
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_stop_apply ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_stop_apply(operation: u32, index: u32,
    generation: u64) -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: u32::MAX, woken: 0 };
    if ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
        output.reply = reply(9); return output;
    }
    let _guard = Guard;
    // SAFETY: trusted context callback before creating any storage reference.
    if unsafe { kernel_scheduler_context_valid() } != 1 { output.reply = reply(10); return output; }
    if operation > 1 { output.reply = reply(12); return output; }
    // SAFETY: ENTERED owns the storage; no callback or switch during this borrow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    let handle = Handle { index, generation };
    let result = if operation == 0 {
        owner.request_stop(handle).map(|woken| (1, woken))
    } else { owner.stop_requested(handle).map(|stopped| (u32::from(stopped), 0)) };
    match result {
        Ok((outcome, woken)) => {
            let Ok(state) = owner.state(handle) else { output.reply = reply(1); return output; };
            output.reply.index = index; output.reply.generation = generation;
            output.reply.state = state as u32; output.outcome = outcome; output.woken = woken;
        }
        Err(error) => output.reply = reply(error as u32),
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_irq_post ======

// Private trusted ISR API: backend verifies bound source/lifetime before entry.
// No owner access, borrow, callback, allocation, diagnostic or panic path.
#[unsafe(no_mangle)]
#[cfg(scheduler_host_unbound)]
pub extern "C" fn kernel_scheduler_irq_post(event: u32) -> u32 {
    u32::from(PENDING.post(event))
}

// ====== gateway_irq_drain ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_irq_drain() -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: 4, woken: 0 };
    if ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
        output.reply = reply(9); return output;
    }
    let _guard = Guard;
    // SAFETY: trusted ABI callback before creating the exclusive owner borrow.
    if unsafe { kernel_scheduler_context_valid() } != 1 { output.reply = reply(10); return output; }
    // SAFETY: identical ENTERED discipline; no callbacks while this borrow exists.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    if PENDING.empty() { return output; }
    let mask = PENDING.take();
    match owner.signal_pending(mask) {
        Ok(result) => output.woken = result.woken,
        Err(error) => {
            // Retain all notifications on rejection; concurrent posts stay pending.
            if !PENDING.restore(mask) { output.reply = reply(1); return output; }
            output.reply = reply(error as u32);
        }
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_panic ======

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    // SAFETY: native terminal backend must never return. No reset/cleanup here.
    unsafe { kernel_scheduler_fail_stop() }
}
