use super::{Error, Handle, Owner, State, CAPACITY};
const MAX_PARTICIPANTS: u32 = 4;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) struct Lot {
    pub group: u64,
    pub token: u64,
    pub participants: u64,
    pub entered: u64,
    pub completed: u64,
    pub quiesced: u64,
    pub failed: bool,
    tasks: [Option<Handle>; CAPACITY],
    cpus: [u32; CAPACITY],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LotSnapshot {
    pub group: u64,
    pub token: u64,
    pub participants: u64,
    pub entered: u64,
    pub completed: u64,
    pub quiesced: u64,
    pub failed: bool,
}

impl Owner {
    // ====== owner_validate_lot ======

    pub(super) fn validate_lot(&self) -> bool {
        let Some(lot) = self.lot else { return true; };
        if lot.group == 0 || lot.token == 0 || lot.token != self.last_lot ||
            lot.participants & 1 != 0 || !(2..=MAX_PARTICIPANTS).contains(&lot.participants.count_ones()) ||
            lot.participants & !self.all_cpus() != 0 ||
            lot.entered & !lot.participants != 0 || lot.completed & !lot.entered != 0 ||
            lot.quiesced & !lot.completed != 0 { return false; }
        let mut counts = [0u32; 64];
        for (index, member) in lot.tasks.iter().enumerate() {
            let slot = self.slots[index];
            let Some(handle) = member else {
                if !matches!(slot.state, State::Free | State::Retired) { return false; }
                continue;
            };
            let cpu = lot.cpus[index];
            if cpu >= self.cpus || handle.index as usize != index ||
                handle.generation != slot.generation || lot.participants & (1 << cpu) == 0 {
                return false;
            }
            counts[cpu as usize] += 1;
            if slot.state == State::Free {
                if lot.quiesced != lot.participants { return false; }
            } else if slot.affinity != 1 << cpu ||
                !matches!(slot.state, State::Ready | State::Running | State::Finished) ||
                (lot.completed & (1 << cpu) != 0 && slot.state != State::Finished) {
                return false;
            }
        }
        (0..self.cpus).all(|cpu| counts[cpu as usize] ==
            if lot.participants & (1 << cpu) != 0 { 2 } else { 0 })
    }

    // ====== owner_lot_open ======

    pub fn lot_open(&mut self, caller: u32, group: u64, token: u64, participants: u64) -> Result<(), Error> {
        self.transaction(|next| {
            next.dispatcher(caller)?;
            if caller != 0 || next.lot.is_some() || next.current.iter().any(Option::is_some) ||
                group == 0 || token == 0 || token <= next.last_lot || participants & 1 != 0 ||
                !(2..=MAX_PARTICIPANTS).contains(&participants.count_ones()) || participants & !next.all_cpus() != 0 {
                return Err(Error::InvalidState);
            }
            let mut lot = Lot { group, token, participants, entered: 0, completed: 0,
                quiesced: 0, failed: false, tasks: [None; CAPACITY], cpus: [0; CAPACITY] };
            let mut counts = [0u32; 64];
            for (index, slot) in next.slots.iter().enumerate() {
                if matches!(slot.state, State::Free | State::Retired) { continue; }
                if slot.state != State::Ready || slot.affinity.count_ones() != 1 ||
                    slot.affinity & participants == 0 { return Err(Error::InvalidState); }
                let cpu = slot.affinity.trailing_zeros();
                lot.tasks[index] = Some(Handle { index: index as u32, generation: slot.generation });
                lot.cpus[index] = cpu;
                counts[cpu as usize] += 1;
            }
            if (0..next.cpus).any(|cpu| counts[cpu as usize] !=
                if participants & (1 << cpu) != 0 { 2 } else { 0 }) {
                return Err(Error::InvalidState);
            }
            next.lot = Some(lot);
            next.last_lot = token;
            Ok(())
        })
    }

    // ====== owner_lot_transition ======

    pub fn lot_transition(&mut self, caller: u32, group: u64, token: u64, operation: u32) -> Result<(), Error> {
        self.transaction(|next| {
            let cpu = if operation == 5 { next.cpu(caller)? } else { next.dispatcher(caller)? };
            let mut lot = next.lot.ok_or(Error::InvalidState)?;
            if group != lot.group || token != lot.token { return Err(Error::InvalidToken); }
            if operation == 5 {
                if caller != 0 && lot.participants & (1 << cpu) == 0 { return Err(Error::InvalidCpu); }
                lot.failed = true;
            } else {
                if lot.failed { return Err(Error::InvalidState); }
                let bit = 1u64 << cpu;
                if operation == 6 {
                    if caller != 0 || lot.quiesced != lot.participants ||
                        lot.tasks.iter().flatten().any(|h| next.slots[h.index as usize].state != State::Free) {
                        return Err(Error::InvalidState);
                    }
                    next.lot = None;
                    return Ok(());
                }
                if lot.participants & bit == 0 { return Err(Error::InvalidCpu); }
                match operation {
                    1 if lot.entered & bit == 0 => lot.entered |= bit,
                    2 if lot.entered & bit != 0 && lot.completed & bit == 0 => {
                        if lot.tasks.iter().enumerate().any(|(i, h)| h.is_some() &&
                            lot.cpus[i] == caller && next.slots[i].state != State::Finished) {
                            return Err(Error::InvalidState);
                        }
                        lot.completed |= bit;
                    }
                    3 if lot.completed & bit != 0 && lot.quiesced & bit == 0 => lot.quiesced |= bit,
                    _ => return Err(Error::InvalidState),
                }
            }
            next.lot = Some(lot);
            Ok(())
        })
    }

    // ====== owner_lot_snapshot ======

    pub fn lot_snapshot(&self, caller: u32, group: u64, token: u64) -> Result<LotSnapshot, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        self.cpu(caller)?;
        let lot = self.lot.ok_or(Error::InvalidState)?;
        if group != lot.group || token != lot.token { return Err(Error::InvalidToken); }
        if caller != 0 && lot.participants & (1 << caller) == 0 { return Err(Error::InvalidCpu); }
        Ok(LotSnapshot { group, token, participants: lot.participants, entered: lot.entered,
            completed: lot.completed, quiesced: lot.quiesced, failed: lot.failed })
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/lot_test.rs"]
mod tests;
