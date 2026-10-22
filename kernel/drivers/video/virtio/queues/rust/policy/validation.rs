#![forbid(unsafe_code)]

// ====== register ======

pub fn register(offset: u32, width: u32, write: u32) -> u32 {
    if write > 1 { return 0; }
    let valid = match (offset, width) {
        (0x10 | 0x16 | 0x18 | 0x1a | 0x1c, 2) | (0x14, 1) => true,
        (0x12 | 0x1e, 2) => write == 0,
        (0x20 | 0x24 | 0x28 | 0x2c | 0x30 | 0x34, 4) => true,
        _ => false,
    };
    u32::from(valid)
}

// ====== size ======

pub fn size(maximum: u32) -> u32 {
    u32::from((8..=32768).contains(&maximum) && maximum.is_power_of_two())
}

// ====== completion ======

pub fn completion(old: u32, current: u32, id: u32, bytes: u32) -> u32 {
    completion_sized(old,current,id,bytes,408)
}

// ====== completion_sized ======

pub fn completion_sized(old:u32,current:u32,id:u32,bytes:u32,expected:u32)->u32 {
    u32::from(old <= 65535 && current <= 65535 &&
        (current as u16).wrapping_sub(old as u16) == 1 && id == 0 && bytes == expected && (expected==24 || expected==408))
}

// ====== message ======

pub fn message(command:u32,bytes:u32)->u32 {
    let expected=match command {0x100=>0,0x101=>16,0x103|0x104|0x106=>24,0x105=>32,0x107|0x102=>8,_=>u32::MAX};
    u32::from(expected!=u32::MAX && bytes==expected)
}

// ====== display ======

pub fn display(x: u32, y: u32, width: u32, height: u32, enabled: u32, flags: u32) -> u32 {
    if enabled > 1 || flags != 0 { return 0; }
    if enabled == 0 { return 1; }
    u32::from((1..=16384).contains(&width) && (1..=16384).contains(&height) &&
        x.checked_add(width).is_some() && y.checked_add(height).is_some())
}

#[cfg(test)]
#[path="../../../../../../../test/kernel/drivers/video/virtio/queues/rust/policy/validation_test.rs"]
mod tests;
