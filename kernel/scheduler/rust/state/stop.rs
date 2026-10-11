use super::{Error, Handle, Owner, State};

impl Owner {
    // ====== owner_validate_stops ======

    pub(super) fn validate_stops(&self) -> bool {
        self.slots.iter().zip(self.stops.iter()).all(|(slot, stopped)| {
            !stopped || matches!(slot.state, State::Ready | State::Running | State::Finished)
        })
    }

    // ====== owner_stop_requested ======

    pub fn stop_requested(&self, handle: Handle) -> Result<bool, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        let index = self.handle(handle)?;
        if !matches!(self.slots[index].state, State::Ready | State::Running |
            State::Waiting | State::Finished) { return Err(Error::InvalidState); }
        Ok(self.stops[index])
    }

    // ====== owner_request_stop ======

    // Dispatcher only. Stop never completes a task or releases native resources.
    // Duplicate requests are true no-ops, including at the mutation counter limit.
    pub fn request_stop(&mut self, handle: Handle) -> Result<u32, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        if self.current.is_some() { return Err(Error::Running); }
        let index = self.handle(handle)?;
        if !matches!(self.slots[index].state, State::Ready | State::Waiting) {
            return Err(Error::InvalidState);
        }
        if self.stops[index] { return Ok(0); }
        self.transaction(|next| {
            let waiting = next.slots[index].state == State::Waiting;
            if waiting {
                if !next.ready.push(handle.index) { return Err(Error::Corrupt); }
                next.waits[index] = None;
                next.slots[index].state = State::Ready;
            }
            next.stops[index] = true;
            Ok(u32::from(waiting))
        })
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/stop/stop_test.rs"]
mod stop_test;
