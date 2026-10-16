#![forbid(unsafe_code)]

pub const PENDING: u64 = 1 << 24;
pub const INVALID: u64 = 1 << 25;
pub const OVERFLOW: u64 = 1 << 26;

// ====== mouse_state_valid ======

fn valid(state: u32) -> bool {
    match state & 3 {
        0 => state == 0,
        1 => state & !0x0000_ff01 == 0 && state & 0x800 != 0,
        2 => state & !0x00ff_ff02 == 0 && state & 0x800 != 0,
        _ => false,
    }
}

// ====== mouse_decode_step ======

pub fn step(state: u32, byte: u32) -> u64 {
    if !valid(state) || byte > 255 {
        return INVALID;
    }
    match state & 3 {
        0 => {
            if byte & 8 == 0 {
                return INVALID;
            }
            PENDING | (u64::from(1 | (byte << 8)) << 32)
        }
        1 => PENDING | (u64::from((state & 0xff00) | 2 | (byte << 16)) << 32),
        2 => {
            let header = (state >> 8) & 255;
            if header & 0xc0 != 0 {
                return OVERFLOW;
            }
            let x = ((state >> 16) & 255) | ((header & 0x10) << 4);
            let y = byte | ((header & 0x20) << 3);
            u64::from(x | (y << 9) | ((header & 7) << 18))
        }
        _ => INVALID,
    }
}

#[cfg(test)]
#[path = "../../../../../../../test/kernel/drivers/input/ps2/mouse/rust/packet/decode_test.rs"]
mod tests;
