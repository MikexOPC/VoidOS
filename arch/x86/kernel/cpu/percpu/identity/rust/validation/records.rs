#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]

// ====== xi_record_state ======

// ACPI 6.5 5.2.12.2 Tables 5.22/23, 5.2.12.12 Table 5.34.
// Enabled bit 0; Online Capable bit 1 is reserved when Enabled is set.
// xAPIC bootstrap backend: enabled IDs must fit its unicast destination.
#[unsafe(no_mangle)]
pub extern "C" fn xi_record_state(uid: u32, id: u32, flags: u32, kind: u32) -> u32 {
    if !matches!(kind, 0 | 9) || flags > 2 { return 0; }
    if flags == 0 { return 1; }
    if kind == 0 && (uid > 255 || id > 255) { return 0; }
    if flags == 2 { return 2; }
    if id >= 255 { return 0; }
    3
}

// ====== xi_distinct ======

#[unsafe(no_mangle)]
pub extern "C" fn xi_distinct(uid: u32, id: u32, other_uid: u32, other_id: u32) -> u32 {
    u32::from(uid != other_uid && id != other_id)
}

// ====== tests ======

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn flags_and_widths() {
        assert_eq!(xi_record_state(1, 4, 1, 0), 3);
        assert_eq!(xi_record_state(u32::MAX, 4, 1, 9), 3);
        assert_eq!(xi_record_state(256, 4, 1, 0), 0);
        assert_eq!(xi_record_state(1, 255, 1, 0), 0);
        assert_eq!(xi_record_state(1, 256, 1, 9), 0);
        assert_eq!(xi_record_state(1, 1024, 2, 9), 2);
        assert_eq!(xi_record_state(u32::MAX, u32::MAX, 0, 0), 1);
        for flags in [3, 4, u32::MAX] { assert_eq!(xi_record_state(1, 4, flags, 0), 0); }
        assert_eq!(xi_record_state(1, 4, 1, 8), 0);
    }
    #[test]
    fn identity_is_not_an_index() {
        for id in [0, 7, 128, 254] { assert_eq!(xi_record_state(200, id, 1, 0), 3); }
        assert_eq!(xi_distinct(3, 7, 4, 8), 1);
        assert_eq!(xi_distinct(3, 7, 3, 8), 0);
        assert_eq!(xi_distinct(3, 7, 4, 7), 0);
    }
}
