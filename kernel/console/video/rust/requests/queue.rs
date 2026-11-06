#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

pub const CAPACITY: usize = 8;
pub const TEXT_LIMIT: usize = 256;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation { Write, Edit, Submit }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State { Open, Draining, Closed, Failed }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error { Invalid, Full, Empty, State, InFlight, Pending }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Request {
    operation: Operation,
    length: usize,
    bytes: [u8; TEXT_LIMIT],
}

impl Request {
    const EMPTY: Self = Self {
        operation: Operation::Submit, length: 0, bytes: [0; TEXT_LIMIT],
    };

    // ====== operation ======

    pub fn operation(&self) -> Operation { self.operation }

    // ====== text ======

    pub fn text(&self) -> &[u8] { &self.bytes[..self.length] }

    // ====== prepare ======

    fn prepare(operation: Operation, text: &[u8]) -> Result<Self, Error> {
        if text.len() > TEXT_LIMIT || (operation == Operation::Submit && !text.is_empty()) {
            return Err(Error::Invalid);
        }
        let decoded = core::str::from_utf8(text).map_err(|_| Error::Invalid)?;
        if decoded.chars().any(|scalar| {
            (scalar < ' ' || ('\u{7f}'..='\u{9f}').contains(&scalar))
                && !(operation == Operation::Write && matches!(scalar, '\n' | '\r'))
        }) {
            return Err(Error::Invalid);
        }
        let mut request = Self::EMPTY;
        request.operation = operation;
        request.length = text.len();
        request.bytes[..text.len()].copy_from_slice(text);
        Ok(request)
    }
}

pub struct Queue {
    entries: [Request; CAPACITY],
    head: usize,
    count: usize,
    in_flight: bool,
    state: State,
}

impl Default for Queue {
    // ====== default ======

    fn default() -> Self { Self::new() }
}

impl Queue {
    // ====== new ======

    pub const fn new() -> Self {
        Self { entries: [Request::EMPTY; CAPACITY], head: 0, count: 0,
            in_flight: false, state: State::Open }
    }

    // ====== state ======

    pub fn state(&self) -> State { self.state }

    // ====== pending ======

    pub fn pending(&self) -> usize { self.count }

    // ====== push ======

    pub fn push(&mut self, operation: Operation, text: &[u8]) -> Result<(), Error> {
        if self.state != State::Open { return Err(Error::State); }
        let request = Request::prepare(operation, text)?;
        if self.count == CAPACITY { return Err(Error::Full); }
        self.entries[(self.head + self.count) % CAPACITY] = request;
        self.count += 1;
        Ok(())
    }

    // ====== take ======

    pub fn take(&mut self) -> Result<Request, Error> {
        if !matches!(self.state, State::Open | State::Draining) { return Err(Error::State); }
        if self.in_flight { return Err(Error::InFlight); }
        if self.count == 0 { return Err(Error::Empty); }
        let request = self.entries[self.head];
        self.entries[self.head] = Request::EMPTY;
        self.head = (self.head + 1) % CAPACITY;
        self.count -= 1;
        self.in_flight = true;
        Ok(request)
    }

    // ====== complete ======

    pub fn complete(&mut self, success: bool) -> Result<(), Error> {
        if !matches!(self.state, State::Open | State::Draining) { return Err(Error::State); }
        if !self.in_flight { return Err(Error::InFlight); }
        if success { self.in_flight = false; } else { self.state = State::Failed; }
        Ok(())
    }

    // ====== drain ======

    pub fn drain(&mut self) -> Result<(), Error> {
        if self.state != State::Open { return Err(Error::State); }
        self.state = State::Draining;
        Ok(())
    }

    // ====== close ======

    pub fn close(&mut self) -> Result<(), Error> {
        if self.state != State::Draining { return Err(Error::State); }
        if self.in_flight { return Err(Error::InFlight); }
        if self.count != 0 { return Err(Error::Pending); }
        self.state = State::Closed;
        Ok(())
    }
}
