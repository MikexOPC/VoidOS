#![cfg_attr(not(test), no_std)]

// ====== x86_fb_table_entry ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_fb_table_entry(table: u32, slot: u32, physical: u64,
    pages: u64, cache: u64, bits: u32, child: u64) -> u64 {
    if !(32..=52).contains(&bits) || !(1..=4321).contains(&pages) || slot >= 512 ||
        cache & !0x98 != 0 || physical == 0 || physical & 4095 != 0 {
        return u64::MAX;
    }
    let Some(end) = physical.checked_add(pages * 4096) else { return u64::MAX; };
    if end > 1u64 << bits {
        return u64::MAX;
    }
    let pt_count = (pages + 2).div_ceil(512);
    if u64::from(table) >= pt_count + 2 {
        return u64::MAX;
    }
    if table < 2 {
        let present = (table == 0 && slot == 0) || (table == 1 && u64::from(slot) < pt_count);
        if !present {
            return if child == 0 {
                0
            }
            else {
                u64::MAX
            };
        }
        if child == 0 || child & 4095 != 0 || child >= 1u64 << bits { return u64::MAX; }
        return child | 3;
    }
    if child != 0 {
        return u64::MAX;
    }
    let page = u64::from(table - 2) * 512 + u64::from(slot);
    if page == 0 || page > pages {
        return 0;
    }
    (physical + (page - 1) * 4096) | cache | (1 << 63) | 3
}

#[cfg(test)]
#[path = "../../test/rust/tables/contents_test.rs"]
mod tests;
