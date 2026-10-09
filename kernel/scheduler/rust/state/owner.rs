mod queue;
mod wait;
mod stop;
pub use queue::{CAPACITY, NONE};
pub use wait::{EventResult, EVENT_CAPACITY};
use queue::Queue;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum State { Free, Reserved, Ready, Running, Finished, Retired, Waiting }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Handle { pub index: u32, pub generation: u64 }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Error {
    Corrupt = 1, Capacity, InvalidHandle, InvalidState, Running, Empty,
    Retired, CounterExhausted, Blocked = 14, InvalidEvent, InvalidToken,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Slot { generation: u64, state: State }

#[derive(Debug, PartialEq, Eq)]
pub struct Owner {
    slots: [Slot; CAPACITY],
    ready: Queue,
    current: Option<Handle>,
    events: u64,
    epochs: [u64; EVENT_CAPACITY],
    waits: [Option<wait::Token>; CAPACITY],
    stops: [bool; CAPACITY],
}

// ====== owner_default ======

impl Default for Owner {
    fn default() -> Self { Self::new() }
}

// ====== owner_new ======

impl Owner {
    pub const fn new() -> Self {
        Self {
            slots: [Slot { generation: 0, state: State::Free }; CAPACITY],
            ready: Queue::new(), current: None, events: 0,
            epochs: [0; EVENT_CAPACITY], waits: [None; CAPACITY], stops: [false; CAPACITY],
        }
    }

    // ====== owner_validate ======

    pub fn validate(&self) -> bool {
        if !self.ready.validate() || !self.validate_waits() || !self.validate_stops() { return false; }
        let mut ready = 0;
        let mut running = 0;
        for (index, slot) in self.slots.iter().enumerate() {
            if slot.state != State::Free && slot.generation == 0 { return false; }
            if slot.state == State::Retired && slot.generation != u64::MAX {
                return false;
            }
            if (slot.state == State::Ready) != self.ready.contains(index as u32) {
                return false;
            }
            if slot.state == State::Ready { ready += 1; }
            if slot.state == State::Running {
                running += 1;
                if self.current != Some(Handle {
                    index: index as u32, generation: slot.generation,
                }) { return false; }
            }
        }
        ready == self.ready.len() && running == usize::from(self.current.is_some())
    }

    // ====== owner_handle ======

    fn handle(&self, handle: Handle) -> Result<usize, Error> {
        let index = handle.index as usize;
        match self.slots.get(index) {
            Some(slot) if handle.generation != 0 && slot.generation == handle.generation => Ok(index),
            _ => Err(Error::InvalidHandle),
        }
    }

    // ====== owner_transaction ======

    // Whole-owner candidate: failed actions cannot partially publish queue/state.
    // Retirement is the sole error-shaped committed event, explicitly documented.
    fn transaction<T>(&mut self, action: impl FnOnce(&mut Self) -> Result<T, Error>)
        -> Result<T, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        let mut next = self.copy_state();
        let result = action(&mut next);
        if result.is_ok() || matches!(result, Err(Error::Retired)) {
            next.events = self.events.checked_add(1).ok_or(Error::CounterExhausted)?;
            if !next.validate() { return Err(Error::Corrupt); }
            *self = next;
        }
        result
    }

    // ====== owner_candidate ======

    // Private copy solely for rollback-safe transactions; owner is not public Copy.
    fn copy_state(&self) -> Self {
        Self { slots: self.slots, ready: self.ready, current: self.current, events: self.events,
            epochs: self.epochs, waits: self.waits, stops: self.stops }
    }

    // ====== owner_reserve ======

    pub fn reserve(&mut self) -> Result<Handle, Error> {
        self.transaction(|next| {
            if next.current.is_some() { return Err(Error::Running); }
            let index = next.slots.iter().position(|s| s.state == State::Free)
                .ok_or(Error::Capacity)?;
            let slot = &mut next.slots[index];
            if slot.generation == u64::MAX {
                slot.state = State::Retired;
                return Err(Error::Retired);
            }
            slot.generation += 1;
            slot.state = State::Reserved;
            Ok(Handle { index: index as u32, generation: slot.generation })
        })
    }

    // ====== owner_publish ======

    pub fn publish(&mut self, handle: Handle) -> Result<(), Error> {
        self.transaction(|next| {
            if next.current.is_some() { return Err(Error::Running); }
            let index = next.handle(handle)?;
            if next.slots[index].state != State::Reserved { return Err(Error::InvalidState); }
            if !next.ready.push(handle.index) { return Err(Error::Corrupt); }
            next.slots[index].state = State::Ready;
            Ok(())
        })
    }

    // ====== owner_release ======

    fn release(&mut self, handle: Handle, expected: State) -> Result<(), Error> {
        self.transaction(|next| {
            if next.current.is_some() { return Err(Error::Running); }
            let index = next.handle(handle)?;
            if next.slots[index].state != expected { return Err(Error::InvalidState); }
            next.slots[index].state = State::Free;
            next.stops[index] = false;
            Ok(())
        })
    }

    // ====== owner_abort ======

    // Caller must first confirm native rollback; no native ownership exists here.
    pub fn abort(&mut self, handle: Handle) -> Result<(), Error> {
        self.release(handle, State::Reserved)
    }

    // ====== owner_collect ======

    // Caller must be back on dispatcher and confirm native cleanup beforehand.
    pub fn collect(&mut self, handle: Handle) -> Result<(), Error> {
        self.release(handle, State::Finished)
    }

    // ====== owner_select ======

    pub fn select(&mut self) -> Result<Handle, Error> {
        self.transaction(|next| {
            if next.current.is_some() { return Err(Error::Running); }
            let index = next.ready.pop().ok_or_else(|| {
                if next.waits.iter().any(Option::is_some) { Error::Blocked } else { Error::Empty }
            })?;
            let slot = &mut next.slots[index as usize];
            slot.state = State::Running;
            let handle = Handle { index, generation: slot.generation };
            next.current = Some(handle);
            Ok(handle)
        })
    }

    // ====== owner_complete ======

    fn complete(&mut self, handle: Handle, finish: bool) -> Result<(), Error> {
        self.transaction(|next| {
            let index = next.handle(handle)?;
            if next.current != Some(handle) || next.slots[index].state != State::Running {
                return Err(Error::InvalidState);
            }
            if !finish && !next.ready.push(handle.index) { return Err(Error::Corrupt); }
            next.slots[index].state = if finish { State::Finished } else { State::Ready };
            next.current = None;
            Ok(())
        })
    }

    // ====== owner_yield_task ======

    pub fn yield_task(&mut self, handle: Handle) -> Result<(), Error> {
        self.complete(handle, false)
    }

    // ====== owner_exit ======

    pub fn exit(&mut self, handle: Handle) -> Result<(), Error> {
        self.complete(handle, true)
    }

    // ====== owner_state ======

    pub fn state(&self, handle: Handle) -> Result<State, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        Ok(self.slots[self.handle(handle)?].state)
    }

    // ====== owner_current ======

    pub fn current(&self) -> Result<Handle, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        self.current.ok_or(Error::Empty)
    }

    // ====== owner_snapshot ======

    pub fn snapshot(&self) -> Result<(u32, u64), Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        Ok((self.ready.len() as u32, self.events))
    }
}

#[cfg(test)]
#[path = "../../test/state/owner_test.rs"]
mod owner_test;

#[cfg(test)]
#[path = "../../test/state/model_test.rs"]
mod model_test;

#[cfg(test)]
#[path = "../../test/irq/batch_test.rs"]
mod batch_test;

#[cfg(test)]
#[path = "../irq/pending.rs"]
mod pending;
