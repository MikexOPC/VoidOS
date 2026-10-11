#![forbid(unsafe_code)]
// Italian00000410 reference: Microsoft globalization/keyboards/kbdit.html,
// normal/shift/caps/ctrlalt/shiftctrlalt views. Independently written mapping.
// Physical set2 IDs: QEMU keycodemapdb data/keymaps.csv, inspected2026-10-09.
pub const INVALID: u32 = u32::MAX;
pub const BACKSPACE: u32 = 0x110001;
pub const TAB: u32 = 0x110002;
pub const ENTER: u32 = 0x110003;
const LS: u32 = 1;
const RS: u32 = 2;
const LC: u32 = 4;
const RC: u32 = 8;
const LA: u32 = 16;
const RA: u32 = 32;
const CAPS: u32 = 64;
const CAPS_DOWN: u32 = 128;

// ====== letter ======

fn letter(code: u32) -> Option<u8> {
    Some(match code {
        0x1c => b'a', 0x32 => b'b', 0x21 => b'c', 0x23 => b'd',
        0x24 => b'e', 0x2b => b'f', 0x34 => b'g', 0x33 => b'h',
        0x43 => b'i', 0x3b => b'j', 0x42 => b'k', 0x4b => b'l',
        0x3a => b'm', 0x31 => b'n', 0x44 => b'o', 0x4d => b'p',
        0x15 => b'q', 0x2d => b'r', 0x1b => b's', 0x2c => b't',
        0x3c => b'u', 0x2a => b'v', 0x1d => b'w', 0x22 => b'x',
        0x35 => b'y', 0x1a => b'z', _ => return None,
    })
}

// ====== symbol ======

fn symbol(code: u32, shift: bool) -> u32 {
    let (plain, shifted): (u32, u32) = match code {
        0x0e => (0x5c, 0x7c), 0x16 => (0x31, 0x21),
        0x1e => (0x32, 0x22), 0x26 => (0x33, 0xa3),
        0x25 => (0x34, 0x24), 0x2e => (0x35, 0x25),
        0x36 => (0x36, 0x26), 0x3d => (0x37, 0x2f),
        0x3e => (0x38, 0x28), 0x46 => (0x39, 0x29),
        0x45 => (0x30, 0x3d), 0x4e => (0x27, 0x3f),
        0x55 => (0xec, 0x5e), 0x54 => (0xe8, 0xe9),
        0x5b => (0x2b, 0x2a), 0x4c => (0xf2, 0xe7),
        0x52 => (0xe0, 0xb0), 0x5d => (0xf9, 0xa7),
        0x61 => (0x3c, 0x3e), 0x41 => (0x2c, 0x3b),
        0x49 => (0x2e, 0x3a), 0x4a => (0x2d, 0x5f),
        0x29 => (0x20, 0x20), _ => return 0,
    };
    if shift { shifted } else { plain }
}

// ====== altgr ======

fn altgr(code: u32, shift: bool) -> u32 {
    match (code, shift) {
        (0x24 | 0x2e, false) => 0x20ac,
        (0x54, false) => 0x5b, (0x5b, false) => 0x5d,
        (0x4c, false) => 0x40, (0x52, false) => 0x23,
        (0x54, true) => 0x7b, (0x5b, true) => 0x7d,
        _ => 0,
    }
}

// ====== step ======

pub fn step(mut state: u32, event: u32) -> u64 {
    let code = event & 0xffff;
    let action = event >> 16;
    if state & !255 != 0 || !matches!(code, 1..=0x84 | 0x101..=0x184 | 0x200) ||
        !matches!((code, action), (0x200, 3) | (1..=0x184, 1..=2)) {
        return u64::from(INVALID);
    }
    let held = match code {
        0x12 => LS, 0x59 => RS, 0x14 => LC, 0x114 => RC,
        0x11 => LA, 0x111 => RA, _ => 0,
    };
    if held != 0 {
        if action == 1 { state |= held; } else { state &= !held; }
    } else if code == 0x58 {
        if action == 1 {
            if state & CAPS_DOWN == 0 { state ^= CAPS; }
            state |= CAPS_DOWN;
        } else { state &= !CAPS_DOWN; }
    } else if action == 1 && state & (LC | RC | LA) == 0 {
        let shift = state & (LS | RS) != 0;
        let scalar = if state & RA != 0 { altgr(code, shift) }
        else if let Some(lower) = letter(code) {
            u32::from(if shift ^ (state & CAPS != 0) { lower - 32 } else { lower })
        } else {
            match code {
                0x66 => BACKSPACE, 0x0d => TAB, 0x5a => ENTER,
                _ => symbol(code, shift),
            }
        };
        return (u64::from(state) << 32) | u64::from(scalar);
    }
    u64::from(state) << 32
}

#[cfg(test)]
#[path = "../../../../../test/kernel/input/keyboard/text/italian_test.rs"]
mod tests;
