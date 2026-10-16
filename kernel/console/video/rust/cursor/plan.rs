#![cfg_attr(not(test), no_std)]

// ====== kernel_console_plan ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_plan(columns: u32, rows: u32, x: u32, y: u32, scalar: u32) -> u64 {
    if columns == 0 || columns > 240 || rows == 0 || rows > 67 || x > columns || y >= rows {
        return u64::MAX;
    }
    let mut next_x = x;
    let mut next_y = y;
    let mut action = 0_u32;
    match scalar {
        8 => {
            if next_x > 0 {
                next_x -= 1;
                action = 2;
            } else if next_y > 0 {
                next_y -= 1;
                next_x = columns - 1;
                action = 2;
            }
        }
        13 => next_x = 0,
        10 => {
            next_x = 0;
            if next_y.saturating_add(1) == rows { action = 4; } else { next_y = next_y.saturating_add(1); }
        }
        _ => {
            if char::from_u32(scalar).is_none() || scalar < 32 || (127..=159).contains(&scalar) {
                return u64::MAX;
            }
            if next_x == columns {
                next_x = 0;
                if next_y.saturating_add(1) == rows { action = 4; } else { next_y = next_y.saturating_add(1); }
            }
            next_x = next_x.saturating_add(1);
            action |= 1;
        }
    }
    u64::from(next_x) | (u64::from(next_y) << 16) | (u64::from(action) << 32)
}

// ====== kernel_console_caret_index ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_caret_index(columns: u32, rows: u32, x: u32, y: u32) -> u32 {
    if columns == 0 || columns > 240 || rows == 0 || rows > 67 || x > columns || y >= rows {
        return u32::MAX;
    }
    y.saturating_mul(columns).saturating_add(x.min(columns.saturating_sub(1)))
}
