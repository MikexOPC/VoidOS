use super::{Error, Handle, Owner, State};

pub const CPUS: usize = 64;
pub const SLICE_MAX: u32 = 1_000_000;

impl Owner {
    // ====== owner_cpu ======

    pub(super) fn cpu(&self, cpu: u32) -> Result<usize, Error> {
        if cpu < self.cpus { Ok(cpu as usize) } else { Err(Error::InvalidCpu) }
    }

    // ====== owner_dispatcher ======

    pub(super) fn dispatcher(&self, cpu: u32) -> Result<usize, Error> {
        let at = self.cpu(cpu)?;
        if self.current[at].is_some() { Err(Error::Running) } else { Ok(at) }
    }

    // ====== owner_all_cpus ======

    pub(super) fn all_cpus(&self) -> u64 {
        if self.cpus as usize >= CPUS { u64::MAX } else { (1u64 << self.cpus) - 1 }
    }

    // ====== owner_busy ======

    pub(super) fn busy(&self) -> bool {
        self.waits.iter().any(Option::is_some) || self.ready.len() != 0 ||
            self.current.iter().any(Option::is_some)
    }

    // ====== owner_validate_cpus ======

    pub(super) fn validate_cpus(&self) -> bool {
        if self.cpus == 0 || self.cpus as usize > CPUS || self.slice == 0 || self.slice > SLICE_MAX {
            return false;
        }
        (0..CPUS).all(|cpu| match self.current[cpu] {
            Some(handle) => cpu < self.cpus as usize && self.remaining[cpu] <= self.slice &&
                self.slots.get(handle.index as usize).is_some_and(|slot|
                    slot.generation == handle.generation && slot.state == State::Running &&
                    slot.affinity & (1 << cpu) != 0),
            None => self.remaining[cpu] == 0,
        })
    }

    // ====== owner_configure ======

    pub fn configure(&mut self, cpus: u32, slice: u32) -> Result<(), Error> {
        self.transaction(|next| {
            if cpus == 0 || cpus as usize > CPUS || slice == 0 || slice > SLICE_MAX {
                return Err(Error::InvalidCpu);
            }
            if next.slots.iter().any(|slot| !matches!(slot.state, State::Free | State::Retired)) {
                return Err(Error::Running);
            }
            next.cpus = cpus;
            next.slice = slice;
            Ok(())
        })
    }

    // ====== owner_set_affinity ======

    pub fn set_affinity_on(&mut self, cpu: u32, handle: Handle, mask: u64) -> Result<(), Error> {
        self.transaction(|next| {
            next.dispatcher(cpu)?;
            let index = next.handle(handle)?;
            if next.slots[index].state != State::Reserved { return Err(Error::InvalidState); }
            if mask == 0 || mask & !next.all_cpus() != 0 { return Err(Error::InvalidCpu); }
            next.slots[index].affinity = mask;
            Ok(())
        })
    }

    // ====== owner_affinity ======

    #[cfg(test)]
    pub fn affinity(&self, handle: Handle) -> Result<u64, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        Ok(self.slots[self.handle(handle)?].affinity)
    }

    // ====== owner_tick ======

    pub fn tick_on(&mut self, cpu: u32) -> Result<bool, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        let at = self.cpu(cpu)?;
        if self.current[at].is_none() { return Ok(false); }
        if self.remaining[at] > 0 {
            self.transaction(|next| { next.remaining[at] -= 1; Ok(()) })?;
        }
        Ok(self.remaining[at] == 0 && self.ready_for(cpu))
    }

    // ====== owner_ready_for ======

    pub(super) fn ready_for(&self, cpu: u32) -> bool {
        (0..self.slots.len()).any(|index| self.slots[index].state == State::Ready &&
            self.slots[index].affinity & (1u64 << cpu) != 0)
    }

    // ====== owner_cpus ======

    #[cfg(test)]
    pub fn cpus(&self) -> u32 { self.cpus }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/smp_test.rs"]
mod smp_test;
