use super::{Error, Handle, Owner, State};
pub const EVENT_CAPACITY: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) struct Token { event: usize, epoch: u64 }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct EventResult { pub epoch: u64, pub outcome: u32, pub woken: u32 }

impl Owner {
    // ====== owner_validate_waits ======

    pub(super) fn validate_waits(&self) -> bool {
        self.slots.iter().zip(self.waits.iter()).all(|(slot, token)| match token {
            Some(token) => slot.state == State::Waiting && token.event < EVENT_CAPACITY &&
                token.epoch == self.epochs[token.event],
            None => slot.state != State::Waiting,
        })
    }

    // ====== owner_event_actor ======

    fn event_actor(&self, handle: Handle, event: u32) -> Result<usize, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        if event as usize >= EVENT_CAPACITY { return Err(Error::InvalidEvent); }
        let index = self.handle(handle)?;
        if self.current != Some(handle) || self.slots[index].state != State::Running {
            return Err(Error::InvalidState);
        }
        Ok(index)
    }

    // ====== owner_observe ======

    pub fn observe(&self, handle: Handle, event: u32) -> Result<EventResult, Error> {
        self.event_actor(handle, event)?;
        Ok(EventResult { epoch: self.epochs[event as usize], outcome: 0, woken: 0 })
    }

    // ====== owner_wait ======

    pub fn wait(&mut self, handle: Handle, event: u32, observed: u64) -> Result<EventResult, Error> {
        self.transaction(|next| {
            let index = next.event_actor(handle, event)?;
            let epoch = next.epochs[event as usize];
            if observed > epoch { return Err(Error::InvalidToken); }
            if next.stops[index] {
                if !next.ready.push(handle.index) { return Err(Error::Corrupt); }
                next.slots[index].state = State::Ready;
                next.current = None;
                return Ok(EventResult { epoch, outcome: 5, woken: 0 });
            }
            let changed = observed != epoch;
            if changed {
                if !next.ready.push(handle.index) { return Err(Error::Corrupt); }
                next.slots[index].state = State::Ready;
            } else {
                next.slots[index].state = State::Waiting;
                next.waits[index] = Some(Token { event: event as usize, epoch });
            }
            next.current = None;
            Ok(EventResult { epoch, outcome: if changed { 2 } else { 1 }, woken: 0 })
        })
    }

    // ====== owner_signal ======

    pub fn signal(&mut self, handle: Handle, event: u32) -> Result<EventResult, Error> {
        self.transaction(|next| {
            next.event_actor(handle, event)?;
            let event = event as usize;
            let epoch = next.epochs[event].checked_add(1).ok_or(Error::CounterExhausted)?;
            let mut woken = 0;
            for index in 0..next.slots.len() {
                if next.waits[index].is_some_and(|token| token.event == event) {
                    if !next.ready.push(index as u32) { return Err(Error::Corrupt); }
                    next.waits[index] = None;
                    next.slots[index].state = State::Ready;
                    woken += 1;
                }
            }
            next.epochs[event] = epoch;
            Ok(EventResult { epoch, outcome: 3, woken })
        })
    }

    // ====== owner_signal_pending ======

    // Dispatcher only: no fictitious RUNNING producer. Whole batch is atomic.
    pub fn signal_pending(&mut self, mask: u32) -> Result<EventResult, Error> {
        self.transaction(|next| {
            if next.current.is_some() { return Err(Error::Running); }
            if mask == 0 || mask & !0xFF != 0 { return Err(Error::InvalidEvent); }
            for event in 0..EVENT_CAPACITY {
                if mask & (1 << event) != 0 {
                    next.epochs[event] = next.epochs[event].checked_add(1).ok_or(Error::CounterExhausted)?;
                }
            }
            let mut woken = 0;
            for index in 0..next.slots.len() {
                if next.waits[index].is_some_and(|token| mask & (1 << token.event) != 0) {
                    if !next.ready.push(index as u32) { return Err(Error::Corrupt); }
                    next.waits[index] = None;
                    next.slots[index].state = State::Ready;
                    woken += 1;
                }
            }
            Ok(EventResult { epoch: 0, outcome: 4, woken })
        })
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/wait_test.rs"]
mod wait_test;

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/wait_model.rs"]
mod wait_model;
