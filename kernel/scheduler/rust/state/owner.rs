mod queue;
mod wait;
mod stop;
mod smp;
mod lot;
pub use lot::LotSnapshot;
pub use queue::{CAPACITY, MAX_CAPACITY, MIN_CAPACITY, NONE};
pub use wait::{EventResult, EVENT_CAPACITY};
pub use smp::CPUS;
use core::ops::{Deref, DerefMut};
use queue::Ring;

pub const STORAGE_ALIGN: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum State { Free, Reserved, Ready, Running, Finished, Retired, Waiting }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Handle { pub index: u32, pub generation: u64 }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Error {
    Corrupt = 1, Capacity, InvalidHandle, InvalidState, Running, Empty,
    Retired, CounterExhausted, BadArgument = 12, Blocked = 14, InvalidEvent, InvalidToken, InvalidCpu,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Slot { generation: u64, state: State, affinity: u64 }

// Typed window over `len` initialized elements of the owner storage. The owner keeps the windows
// of one bank disjoint from the other bank, so a copy never aliases a live mutable use.
struct Table<T> { ptr: *mut T, len: usize }

// ====== table_clone ======

impl<T> Clone for Table<T> {
    fn clone(&self) -> Self { *self }
}

impl<T> Copy for Table<T> {}

// ====== table_deref ======

impl<T> Deref for Table<T> {
    type Target = [T];

    fn deref(&self) -> &[T] {
        // SAFETY: the owner binds `ptr` to `len` initialized elements of its live storage.
        unsafe { core::slice::from_raw_parts(self.ptr, self.len) }
    }
}

// ====== table_deref_mut ======

impl<T> DerefMut for Table<T> {
    fn deref_mut(&mut self) -> &mut [T] {
        // SAFETY: same binding as deref; the bank region is used through one view at a time.
        unsafe { core::slice::from_raw_parts_mut(self.ptr, self.len) }
    }
}

// ====== table_is_bound ======

impl<T> Table<T> {
    fn is_bound(&self, ptr: *mut T, len: usize) -> bool { self.ptr == ptr && self.len == len }
}

// ====== table_eq ======

impl<T: PartialEq> PartialEq for Table<T> {
    fn eq(&self, other: &Self) -> bool { **self == **other }
}

impl<T: Eq> Eq for Table<T> {}

// ====== table_debug ======

impl<T: core::fmt::Debug> core::fmt::Debug for Table<T> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

// Storage of one owner: two banks (active and scratch) of the same layout. A transaction copies
// the active bank into the scratch bank, mutates the copy and publishes it by flipping the bank.
#[derive(Clone, Copy)]
struct Layout { waits: usize, lot_tasks: usize, ready: usize, lot_cpus: usize, stops: usize, bank: usize, total: usize }

const _: () = assert!(
    align_of::<Slot>() <= STORAGE_ALIGN && align_of::<Option<wait::Token>>() <= STORAGE_ALIGN &&
    align_of::<Option<Handle>>() <= STORAGE_ALIGN
);

// ====== align_up ======

const fn align_up(at: usize, align: usize) -> usize { (at + align - 1) & !(align - 1) }

// ====== layout_raw ======

const fn layout_raw(capacity: usize) -> Layout {
    let waits = align_up(capacity * size_of::<Slot>(), align_of::<Option<wait::Token>>());
    let lot_tasks = align_up(waits + capacity * size_of::<Option<wait::Token>>(), align_of::<Option<Handle>>());
    let ready = align_up(lot_tasks + capacity * size_of::<Option<Handle>>(), align_of::<u32>());
    let lot_cpus = align_up(ready + capacity * size_of::<u32>(), align_of::<u32>());
    let stops = lot_cpus + capacity * size_of::<u32>();
    let bank = align_up(stops + capacity * size_of::<bool>(), STORAGE_ALIGN);
    Layout { waits, lot_tasks, ready, lot_cpus, stops, bank, total: bank * 2 }
}

// ====== layout ======

fn layout(capacity: usize) -> Option<Layout> {
    if (MIN_CAPACITY..=MAX_CAPACITY).contains(&capacity) { Some(layout_raw(capacity)) } else { None }
}

// ====== storage_bytes ======

pub fn storage_bytes(capacity: usize) -> Option<usize> { layout(capacity).map(|layout| layout.total) }

// ====== default_storage_bytes ======

#[cfg(not(test))]
pub const fn default_storage_bytes() -> usize { layout_raw(CAPACITY).total }

struct BankPtrs {
    slots: *mut Slot,
    waits: *mut Option<wait::Token>,
    lot_tasks: *mut Option<Handle>,
    ready: *mut u32,
    lot_cpus: *mut u32,
    stops: *mut bool,
}

// ====== bank_ptrs ======

unsafe fn bank_ptrs(base: *mut u8, layout: &Layout, bank: usize) -> BankPtrs {
    // SAFETY: the caller guarantees that `base` spans `layout.total` bytes and `bank` is 0 or 1.
    unsafe {
        let origin = base.add(bank * layout.bank);
        BankPtrs {
            slots: origin.cast(),
            waits: origin.add(layout.waits).cast(),
            lot_tasks: origin.add(layout.lot_tasks).cast(),
            ready: origin.add(layout.ready).cast(),
            lot_cpus: origin.add(layout.lot_cpus).cast(),
            stops: origin.add(layout.stops).cast(),
        }
    }
}

pub struct Owner {
    slots: Table<Slot>,
    ready: Ring,
    current: [Option<Handle>; CPUS],
    remaining: [u32; CPUS],
    cpus: u32,
    slice: u32,
    events: u64,
    epochs: [u64; EVENT_CAPACITY],
    waits: Table<Option<wait::Token>>,
    stops: Table<bool>,
    lot: Option<lot::Lot>,
    last_lot: u64,
    capacity: usize,
    active_bank: usize,
    storage_base: *mut u8,
    storage_bytes: usize,
    #[cfg(test)]
    owned: Option<Vec<u64>>,
}

// ====== owner_eq ======

impl PartialEq for Owner {
    fn eq(&self, other: &Self) -> bool {
        self.capacity == other.capacity && self.slots == other.slots && self.ready == other.ready &&
            self.current == other.current && self.remaining == other.remaining &&
            self.cpus == other.cpus && self.slice == other.slice && self.events == other.events &&
            self.epochs == other.epochs && self.waits == other.waits && self.stops == other.stops &&
            self.lot == other.lot && self.last_lot == other.last_lot
    }
}

impl Eq for Owner {}

// ====== owner_debug ======

impl core::fmt::Debug for Owner {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Owner").field("capacity", &self.capacity).field("slots", &self.slots)
            .field("ready", &self.ready).field("current", &self.current)
            .field("remaining", &self.remaining).field("cpus", &self.cpus)
            .field("slice", &self.slice).field("events", &self.events)
            .field("epochs", &self.epochs).field("waits", &self.waits)
            .field("stops", &self.stops).field("lot", &self.lot)
            .field("last_lot", &self.last_lot).finish()
    }
}

// ====== owner_default ======

#[cfg(test)]
impl Default for Owner {
    fn default() -> Self { Self::new() }
}

// ====== owner_new ======

impl Owner {
    #[cfg(test)]
    pub fn new() -> Self {
        Self::with_capacity(CAPACITY).expect("default capacity is valid")
    }

    // ====== owner_with_capacity ======

    #[cfg(test)]
    pub fn with_capacity(capacity: usize) -> Result<Self, Error> {
        let bytes = storage_bytes(capacity).ok_or(Error::BadArgument)?;
        let mut owned = vec![0u64; bytes.div_ceil(8)];
        let base = owned.as_mut_ptr().cast::<u8>();
        // SAFETY: `owned` is 8-aligned, spans `bytes` bytes and lives as long as the owner.
        let mut owner = unsafe { Self::from_storage(base, bytes, capacity)? };
        owner.owned = Some(owned);
        Ok(owner)
    }

    // ====== owner_from_storage ======

    /// Builds an owner on caller storage.
    ///
    /// # Safety
    ///
    /// `buffer` must be valid for reads and writes of `bytes` bytes for the whole life of the
    /// owner and used by nothing else. Null, misalignment and a short buffer are rejected.
    pub unsafe fn from_storage(buffer: *mut u8, bytes: usize, capacity: usize) -> Result<Self, Error> {
        let layout = layout(capacity).ok_or(Error::BadArgument)?;
        if buffer.is_null() || !(buffer as usize).is_multiple_of(STORAGE_ALIGN) || bytes < layout.total {
            return Err(Error::BadArgument);
        }
        for bank in 0..2 {
            // SAFETY: `buffer` spans `layout.total` bytes (checked above) and `bank` is below 2.
            let p = unsafe { bank_ptrs(buffer, &layout, bank) };
            for index in 0..capacity {
                // SAFETY: every region holds `capacity` elements inside the checked span.
                unsafe {
                    p.slots.add(index).write(Slot { generation: 0, state: State::Free, affinity: 0 });
                    p.waits.add(index).write(None);
                    p.lot_tasks.add(index).write(None);
                    p.ready.add(index).write(NONE);
                    p.lot_cpus.add(index).write(0);
                    p.stops.add(index).write(false);
                }
            }
        }
        // SAFETY: `buffer` spans `layout.total` bytes (checked above) and bank 0 is below 2.
        let p = unsafe { bank_ptrs(buffer, &layout, 0) };
        Ok(Self {
            slots: Table { ptr: p.slots, len: capacity },
            ready: Ring { entries: p.ready, cap: capacity, head: 0, count: 0 },
            current: [None; CPUS], remaining: [0; CPUS],
            cpus: 1, slice: 1, events: 0,
            epochs: [0; EVENT_CAPACITY],
            waits: Table { ptr: p.waits, len: capacity },
            stops: Table { ptr: p.stops, len: capacity },
            lot: None, last_lot: 0,
            capacity, active_bank: 0, storage_base: buffer, storage_bytes: bytes,
            #[cfg(test)]
            owned: None,
        })
    }

    // ====== owner_rebased ======

    // Same logical state with every window bound to `bank` of the storage at `base`.
    fn rebased(&self, base: *mut u8, bank: usize, layout: &Layout) -> Self {
        let capacity = self.capacity;
        // SAFETY: `base` spans `layout.total` bytes (checked by caller) and `bank` is 0 or 1.
        let p = unsafe { bank_ptrs(base, layout, bank) };
        Self {
            slots: Table { ptr: p.slots, len: capacity },
            ready: Ring { entries: p.ready, cap: capacity, head: self.ready.head, count: self.ready.count },
            current: self.current, remaining: self.remaining,
            cpus: self.cpus, slice: self.slice, events: self.events,
            epochs: self.epochs,
            waits: Table { ptr: p.waits, len: capacity },
            stops: Table { ptr: p.stops, len: capacity },
            lot: self.lot.map(|lot| lot.rebound(Table { ptr: p.lot_tasks, len: capacity },
                Table { ptr: p.lot_cpus, len: capacity })),
            last_lot: self.last_lot,
            capacity, active_bank: bank, storage_base: base, storage_bytes: self.storage_bytes,
            #[cfg(test)]
            owned: None,
        }
    }

    // ====== owner_lot_tables ======

    fn lot_tables(&self) -> Option<(Table<Option<Handle>>, Table<u32>)> {
        let layout = layout(self.capacity)?;
        if self.active_bank > 1 { return None; }
        // SAFETY: the storage spans `layout.total` bytes (checked at construction) and the bank is 0 or 1.
        let p = unsafe { bank_ptrs(self.storage_base, &layout, self.active_bank) };
        Some((Table { ptr: p.lot_tasks, len: self.capacity }, Table { ptr: p.lot_cpus, len: self.capacity }))
    }

    // ====== owner_storage_valid ======

    fn storage_valid(&self) -> bool {
        let Some(layout) = layout(self.capacity) else { return false; };
        if self.storage_base.is_null() || !(self.storage_base as usize).is_multiple_of(STORAGE_ALIGN) ||
            self.storage_bytes < layout.total || self.active_bank > 1 { return false; }
        // SAFETY: the storage spans `layout.total` bytes (checked above) and the bank is 0 or 1.
        let p = unsafe { bank_ptrs(self.storage_base, &layout, self.active_bank) };
        self.slots.is_bound(p.slots, self.capacity) && self.waits.is_bound(p.waits, self.capacity) &&
            self.stops.is_bound(p.stops, self.capacity) && self.ready.entries == p.ready &&
            self.ready.cap == self.capacity
    }

    // ====== owner_validate ======

    pub fn validate(&self) -> bool {
        if !self.storage_valid() || !self.ready.validate() || !self.validate_waits() ||
            !self.validate_stops() || !self.validate_cpus() || !self.validate_lot() { return false; }
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

    // Whole-owner candidate in the scratch bank: failed actions cannot partially publish
    // queue/state. Retirement is the sole error-shaped committed event, explicitly documented.
    fn transaction<T>(&mut self, action: impl FnOnce(&mut Self) -> Result<T, Error>)
        -> Result<T, Error> {
        if !self.validate() { return Err(Error::Corrupt); }
        let layout = layout(self.capacity).ok_or(Error::Corrupt)?;
        let scratch = self.active_bank ^ 1;
        // SAFETY: both banks lie inside the validated storage and do not overlap.
        unsafe {
            let from = self.storage_base.add(self.active_bank * layout.bank);
            let to = self.storage_base.add(scratch * layout.bank);
            core::ptr::copy_nonoverlapping(from, to, layout.bank);
        }
        let mut next = self.rebased(self.storage_base, scratch, &layout);
        let result = action(&mut next);
        if result.is_ok() || matches!(result, Err(Error::Retired)) {
            next.events = self.events.checked_add(1).ok_or(Error::CounterExhausted)?;
            if !next.validate() { return Err(Error::Corrupt); }
            self.commit(next);
        }
        result
    }

    // ====== owner_commit ======

    fn commit(&mut self, next: Self) {
        #[cfg(test)]
        let owned = self.owned.take();
        *self = next;
        #[cfg(test)]
        {
            self.owned = owned;
        }
    }

    // ====== owner_copy_state ======

    // Independent snapshot with its own storage, solely for tests that compare before and after.
    #[cfg(test)]
    fn copy_state(&self) -> Self {
        let layout = layout(self.capacity).expect("snapshot of a valid layout");
        let mut owned = vec![0u64; self.storage_bytes.div_ceil(8)];
        let base = owned.as_mut_ptr().cast::<u8>();
        // SAFETY: the two allocations are distinct and both span `storage_bytes` bytes.
        unsafe { core::ptr::copy_nonoverlapping(self.storage_base, base, self.storage_bytes) };
        let mut copy = self.rebased(base, self.active_bank, &layout);
        copy.owned = Some(owned);
        copy
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
