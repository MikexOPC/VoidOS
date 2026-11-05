use super::{context_valid_on, core_lock, STORAGE};

#[repr(C)]
pub struct LotReply {
    status: u32,
    failed: u32,
    group: u64,
    token: u64,
    participants: u64,
    entered: u64,
    completed: u64,
    quiesced: u64,
}

const _: () = {
    assert!(core::mem::size_of::<LotReply>() == 56);
    assert!(core::mem::align_of::<LotReply>() == 8);
    assert!(core::mem::offset_of!(LotReply, group) == 8);
    assert!(core::mem::offset_of!(LotReply, quiesced) == 48);
};

// ====== lot_reply ======

fn reply(status: u32) -> LotReply {
    LotReply { status, failed: 0, group: 0, token: 0, participants: 0,
        entered: 0, completed: 0, quiesced: 0 }
}

// ====== gateway_lot_apply ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_lot_on(cpu: u32, operation: u32,
    group: u64, token: u64, participants: u64) -> LotReply {
    let Some(_guard) = core_lock::enter() else { return reply(9); };
    if !context_valid_on(cpu) { return reply(10); }
    if operation > 6 || (operation != 0 && participants != 0) { return reply(12); }
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { return reply(11); };
    let result = match operation {
        0 => owner.lot_open(cpu, group, token, participants),
        4 => Ok(()),
        _ => owner.lot_transition(cpu, group, token, operation),
    };
    if let Err(error) = result { return reply(error as u32); }
    if operation == 6 { return reply(0); }
    match owner.lot_snapshot(cpu, group, token) {
        Ok(super::state::LotSnapshot { group, token, participants, entered, completed, quiesced, failed }) =>
            LotReply { status: 0, failed: u32::from(failed), group, token, participants,
                entered, completed, quiesced },
        Err(error) => reply(error as u32),
    }
}
