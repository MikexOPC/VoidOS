#![cfg_attr(not(test), no_std)]

// ====== xa_geometry_tables ======

#[unsafe(no_mangle)]
pub extern "C" fn xa_geometry_tables(cpus: u32) -> u32 {
    if !(2..=64).contains(&cpus) { return 0; }
    2 + cpus.div_ceil(8)
}

// ====== xa_geometry_page ======

#[unsafe(no_mangle)]
pub extern "C" fn xa_geometry_page(cpus: u32, page: u64) -> u32 {
    if !(2..=64).contains(&cpus) || page >= u64::from(cpus) * 64 { return u32::MAX; }
    let cpu = page / 64;
    if cpu == 0 { return u32::MAX; }
    let local = page % 64;
    let offset = match local {
        1..=16 => local - 1,
        19..=22 => local - 3,
        25..=28 => local - 5,
        31..=34 => local - 7,
        37..=40 => local - 9,
        48 => 32,
        _ => return u32::MAX,
    };
    ((cpu - 1) * 33 + offset) as u32
}

// ====== tests ======

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_cpu_and_page_bounds() {
        for cpus in 1..=64 {
            let mut seen = [false; 2079];
            for page in 0..4096 {
                let value = xa_geometry_page(cpus, page);
                if value != u32::MAX {
                    assert!(!seen[value as usize]); seen[value as usize] = true;
                }
            }
            assert_eq!(seen.iter().filter(|&&v| v).count(), ((cpus - 1) * 33) as usize);
        }
        for cpus in [0, 65, u32::MAX] {
            assert_eq!(xa_geometry_page(cpus, u64::MAX), u32::MAX);
            assert_eq!(xa_geometry_tables(cpus), 0);
        }
    }
    #[test]
    fn budgets_and_guards() {
        assert_eq!(xa_geometry_tables(1), 0);
        assert_eq!(xa_geometry_tables(4), 3);
        assert_eq!(xa_geometry_tables(64), 10);
        for local in [0, 17, 18, 23, 24, 29, 30, 35, 36, 41, 47, 49, 63] {
            assert_eq!(xa_geometry_page(4, 64 + local), u32::MAX);
        }
        assert_eq!(xa_geometry_page(64, 63 * 64 + 48), 2078);
    }
}
