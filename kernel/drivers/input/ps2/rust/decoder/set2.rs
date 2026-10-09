#![forbid(unsafe_code)]

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action { Press, Release, Pulse }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    pub code: u16,
    pub action: Action,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Output { Pending, Key(Key), Invalid }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum State { Idle, Break, Extended, ExtendedBreak, Pause(u8) }

pub const PAUSE: u16 = 0x200;
pub const PRINT_SCREEN: u16 = 0x17c;
const PAUSE_TAIL: [u8; 7] = [0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77];

pub struct Decoder { state: State }

impl Default for Decoder {
    // ====== decoder_default ======

    fn default() -> Self { Self::new() }
}

impl Decoder {
    // ====== decoder_new ======

    pub const fn new() -> Self { Self { state: State::Idle } }

    // ====== decoder_reset ======

    // Invoke on the queue LOSS barrier, transport error or stopped source.
    // A consumer with modifier/pressed state must reset that state too.
    pub fn reset(&mut self) { self.state = State::Idle; }

    // ====== decoder_feed ======

    // One byte, bounded work, at most one event; no replay after malformed input.
    // Codes are opaque physical IDs, not Unicode or indexes into a key table.
    pub fn feed(&mut self, byte: u8) -> Output {
        let state = self.state;
        self.reset();
        if let State::Pause(index) = state {
            if PAUSE_TAIL.get(usize::from(index)) != Some(&byte) {
                return Output::Invalid;
            }
            if usize::from(index) + 1 == PAUSE_TAIL.len() {
                return Output::Key(Key { code: PAUSE, action: Action::Pulse });
            }
            self.state = State::Pause(index + 1);
            return Output::Pending;
        }
        match (state, byte) {
            (State::Idle, 0xe1) => self.state = State::Pause(0),
            (State::Idle, 0xe0) => self.state = State::Extended,
            (State::Idle, 0xf0) => self.state = State::Break,
            (State::Extended, 0xf0) => self.state = State::ExtendedBreak,
            (_, 1..=0x84) => {
                let extended = matches!(state, State::Extended | State::ExtendedBreak);
                // Print Screen uses synthetic extended Shift; never expose it
                // as a modifier. Short Shift/Ctrl Print sequences also work.
                if extended && byte == 0x12 { return Output::Pending; }
                let code = u16::from(byte) | if extended { 0x100 } else { 0 };
                let action = if matches!(state, State::Break | State::ExtendedBreak) {
                    Action::Release
                } else { Action::Press };
                return Output::Key(Key { code, action });
            }
            _ => return Output::Invalid,
        }
        Output::Pending
    }
}

// ====== decoder_step ======

// Integer-only ABI: no caller-owned pointers or Rust enum representations.
// Upper 32 bits hold the next state. Low bits hold code/action or status.
pub fn step(state: u32, byte: u32) -> u64 {
    let state = match state {
        0 => State::Idle,
        1 => State::Break,
        2 => State::Extended,
        3 => State::ExtendedBreak,
        16..=22 => State::Pause((state - 16) as u8),
        _ => return 2 << 24,
    };
    let Ok(byte) = u8::try_from(byte) else { return 2 << 24; };
    let mut decoder = Decoder { state };
    let output = match decoder.feed(byte) {
        Output::Pending => 1 << 24,
        Output::Invalid => 2 << 24,
        Output::Key(key) => u64::from(key.code) | (match key.action {
            Action::Press => 1, Action::Release => 2, Action::Pulse => 3,
        } << 16),
    };
    let next: u64 = match decoder.state {
        State::Idle => 0, State::Break => 1, State::Extended => 2,
        State::ExtendedBreak => 3, State::Pause(index) => 16 + u64::from(index),
    };
    output | (next << 32)
}

#[cfg(test)]
#[path = "../../test/decoder/set2_test.rs"]
mod tests;
