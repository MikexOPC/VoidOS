#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Entry { source: u64, generation: u64 }

#[derive(Debug, PartialEq, Eq)]
pub struct Bindings { entries: [Entry; 8] }

impl Default for Bindings {
    // ====== bindings_default ======

    fn default() -> Self { Self::new() }
}

impl Bindings {
    // ====== bindings_new ======

    pub const fn new() -> Self {
        Self { entries: [Entry { source: 0, generation: 0 }; 8] }
    }

    // ====== bindings_attach ======

    pub fn attach(&mut self, event: u32, source: u64, pending: bool) -> u64 {
        if event >= 8 || source == 0 || pending ||
            self.entries.iter().any(|entry| entry.source == source) { return 0; }
        let entry = &mut self.entries[event as usize];
        if entry.source != 0 { return 0; }
        let Some(generation) = entry.generation.checked_add(1) else { return 0; };
        *entry = Entry { source, generation };
        generation
    }

    // ====== bindings_matches ======

    pub fn matches(&self, event: u32, source: u64, generation: u64) -> bool {
        event < 8 && source != 0 && generation != 0 &&
            self.entries[event as usize] == Entry { source, generation }
    }

    // ====== bindings_detach ======

    pub fn detach(&mut self, event: u32, source: u64, generation: u64, pending: bool) -> bool {
        if pending || !self.matches(event, source, generation) { return false; }
        self.entries[event as usize].source = 0;
        true
    }
}

#[cfg(test)]
#[path = "../../test/irq/bindings_test.rs"]
mod tests;
