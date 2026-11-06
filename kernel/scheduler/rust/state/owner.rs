mod queue;
mod wait;
mod stop;
mod smp;
mod lot;
pub use lot::LotSnapshot;
pub use queue::{CAPACITY, NONE};
pub use wait::{EventResult, EVENT_CAPACITY};
pub use smp::CPUS;
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
    Retired, CounterExhausted, Blocked = 14, InvalidEvent, InvalidToken, InvalidCpu,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Slot { generation: u64, state: State, affinity: u64 }

#[derive(Debug, PartialEq, Eq)]
pub struct Owner {
    slots: [Slot; CAPACITY],
    ready: Queue,
    current: [Option<Handle>; CPUS],
    remaining: [u32; CPUS],
    cpus: u32,
    slice: u32,
    events: u64,
    epochs: [u64; EVENT_CAPACITY],
    waits: [Option<wait::Token>; CAPACITY],
    stops: [bool; CAPACITY],
    lot: Option<lot::Lot>,
    last_lot: u64,
}

// ====== owner_default ======

impl Default for Owner {
    fn default() -> Self { Self::new() }
}

// ====== owner_new ======

impl Owner {
    pub const fn new() -> Self {
        Self {
            slots: [Slot { generation: 0, state: State::Free, affinity: 0 }; CAPACITY],
            ready: Queue::new(), current: [None; CPUS], remaining: [0; CPUS],
            cpus: 1, slice: 1, events: 0,
            epochs: [0; EVENT_CAPACITY], waits: [None; CAPACITY], stops: [false; CAPACITY],
            lot: None, last_lot: 0,
        }
    }

    // ====== owner_validate ======

    pub fn validate(&self) -> bool {
        if !self.ready.validate() || !self.validate_waits() || !self.validate_stops() ||
            !self.validate_cpus() || !self.validate_lot() { return false; }
        let mut ready = 0;
        let mut running = 0;
        for (index, slot) in self.slots.iter().enumerate() {
            if slot.state != State::Free && slot.generation == 0 { return false; }
            if slot.state == State::Retired && slot.generation != u64::MAX {
                return false;
            }
            let unused = matches!(slot.state, State::Free | State::Retired);
            if unused != (slot.affinity == 0) || slot.affinity & !self.all_cpus() != 0 {
                return false;
            }
            if (slot.state == State::Ready) != self.ready.contains(index as u32) {
                return false;
            }
            if slot.state == State::Ready { ready += 1; }
            if slot.state == State::Running {
                running += 1;
                let handle = Handle { index: index as u32, generation: slot.generation };
                if self.current.iter().filter(|current| **current == Some(handle)).count() != 1 {
                    return false;
                }
            }
        }
        ready == self.ready.len() && running == self.current.iter().filter(|c| c.is_some()).count()
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
        Self { slots: self.slots, ready: self.ready, current: self.current,
            remaining: self.remaining, cpus: self.cpus, slice: self.slice, events: self.events,
            epochs: self.epochs, waits: self.waits, stops: self.stops,
            lot: self.lot, last_lot: self.last_lot }
    }

    // ====== owner_reserve ======

    #[cfg(test)]
    pub fn reserve(&mut self) -> Result<Handle, Error> { self.reserve_on(0) }

    pub fn reserve_on(&mut self, cpu: u32) -> Result<Handle, Error> {
        self.transaction(|next| {
            next.dispatcher(cpu)?;
            if next.lot.is_some() { return Err(Error::InvalidState); }
            let index = next.slots.iter().position(|s| s.state == State::Free)
                .ok_or(Error::Capacity)?;
            let all = next.all_cpus();
            let slot = &mut next.slots[index];
            if slot.generation == u64::MAX {
                slot.state = State::Retired;
                return Err(Error::Retired);
            }
            slot.generation += 1;
            slot.state = State::Reserved;
            slot.affinity = all;
            Ok(Handle { index: index as u32, generation: slot.generation })
        })
    }

    // ====== owner_publish ======

    #[cfg(test)]
    pub fn publish(&mut self, handle: Handle) -> Result<(), Error> { self.publish_on(0, handle) }

    pub fn publish_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.transaction(|next| {
            next.dispatcher(cpu)?;
            if next.lot.is_some() { return Err(Error::InvalidState); }
            let index = next.handle(handle)?;
            if next.slots[index].state != State::Reserved { return Err(Error::InvalidState); }
            if !next.ready.push(handle.index) { return Err(Error::Corrupt); }
            next.slots[index].state = State::Ready;
            Ok(())
        })
    }

    // ====== owner_release ======

    fn release(&mut self, cpu: u32, handle: Handle, expected: State) -> Result<(), Error> {
        self.transaction(|next| {
            next.dispatcher(cpu)?;
            if next.lot.is_some_and(|lot| cpu != 0 || expected != State::Finished ||
                lot.failed || lot.quiesced != lot.participants) {
                return Err(Error::InvalidState);
            }
            let index = next.handle(handle)?;
            if next.slots[index].state != expected { return Err(Error::InvalidState); }
            next.slots[index].state = State::Free;
            next.slots[index].affinity = 0;
            next.stops[index] = false;
            Ok(())
        })
    }

    // ====== owner_abort ======

    // Caller must first confirm native rollback; no native ownership exists here.
    #[cfg(test)]
    pub fn abort(&mut self, handle: Handle) -> Result<(), Error> { self.abort_on(0, handle) }

    pub fn abort_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.release(cpu, handle, State::Reserved)
    }

    // ====== owner_collect ======

    // Caller must be back on dispatcher and confirm native cleanup beforehand.
    #[cfg(test)]
    pub fn collect(&mut self, handle: Handle) -> Result<(), Error> { self.collect_on(0, handle) }

    pub fn collect_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.release(cpu, handle, State::Finished)
    }

    // ====== owner_select ======

    #[cfg(test)]
    pub fn select(&mut self) -> Result<Handle, Error> { self.select_on(0) }

    pub fn select_on(&mut self, cpu: u32) -> Result<Handle, Error> {
        self.transaction(|next| {
            let at = next.dispatcher(cpu)?;
            if let Some(lot) = next.lot {
                let bit = 1u64 << cpu;
                if lot.failed || lot.entered & bit == 0 || lot.completed & bit != 0 {
                    return Err(Error::InvalidState);
                }
            }
            let slots = next.slots;
            let index = next.ready.take_first(|i| slots[i as usize].affinity & (1 << cpu) != 0)
                .ok_or_else(|| if next.busy_on(cpu) { Error::Blocked } else { Error::Empty })?;
            let slot = &mut next.slots[index as usize];
            slot.state = State::Running;
            let handle = Handle { index, generation: slot.generation };
            next.current[at] = Some(handle);
            next.remaining[at] = next.slice;
            Ok(handle)
        })
    }

    // ====== owner_complete ======

    fn complete(&mut self, cpu: u32, handle: Handle, finish: bool) -> Result<(), Error> {
        self.transaction(|next| {
            let at = next.cpu(cpu)?;
            let index = next.handle(handle)?;
            if next.current[at] != Some(handle) || next.slots[index].state != State::Running {
                return Err(Error::InvalidState);
            }
            if !finish && !next.ready.push(handle.index) { return Err(Error::Corrupt); }
            next.slots[index].state = if finish { State::Finished } else { State::Ready };
            next.current[at] = None;
            next.remaining[at] = 0;
            Ok(())
        })
    }

    // ====== owner_yield_task ======

    #[cfg(test)]
    pub fn yield_task(&mut self, handle: Handle) -> Result<(), Error> { self.complete(0, handle, false) }

    pub fn yield_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.complete(cpu, handle, false)
    }

    // ====== owner_preempt ======

    pub fn preempt_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.complete(cpu, handle, false)
    }

    // ====== owner_exit ======

    #[cfg(test)]
    pub fn exit(&mut self, handle: Handle) -> Result<(), Error> { self.complete(0, handle, true) }

    pub fn exit_on(&mut self, cpu: u32, handle: Handle) -> Result<(), Error> {
        self.complete(cpu, handle, true)
    }

    // ====== owner_state ======

    pub fn state(&self, handle: Handle) -> Result<State, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        Ok(self.slots[self.handle(handle)?].state)
    }

    // ====== owner_current ======

    #[cfg(test)]
    pub fn current(&self) -> Result<Handle, Error> { self.current_on(0) }

    pub fn current_on(&self, cpu: u32) -> Result<Handle, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        self.current[self.cpu(cpu)?].ok_or(Error::Empty)
    }

    // ====== owner_snapshot ======

    pub fn snapshot(&self) -> Result<(u32, u64), Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        Ok((self.ready.len() as u32, self.events))
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/owner_test.rs"]
mod owner_test;

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/model_test.rs"]
mod model_test;

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/irq/batch_test.rs"]
mod batch_test;

#[cfg(test)]
#[path = "../irq/pending.rs"]
mod pending;
