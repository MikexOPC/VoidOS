#![forbid(unsafe_code)]

pub const VERSION: u8 = 1;
pub const STATUS: u8 = 1;
pub const FRAME_SIZE: usize = 2;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StatusReply {
    Ready = 0,
    Malformed = 1,
    UnsupportedVersion = 2,
    UnsupportedOperation = 3,
}

impl StatusReply {
    // ====== encode ======

    pub const fn encode(self) -> [u8; FRAME_SIZE] {
        [VERSION, self as u8]
    }
}
