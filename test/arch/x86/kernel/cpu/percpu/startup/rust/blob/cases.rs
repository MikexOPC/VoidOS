#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/startup/rust/blob/layout.rs"]
mod layout;
use layout::xs_blob_layout;

// ====== canonical_layout ======

#[test]
fn canonical_layout() {
    assert_eq!(xs_blob_layout(432, 120, 240, 400, 100, 50, 160, 300), 1);
    assert_eq!(xs_blob_layout(4096, 120, 240, 4064, 100, 50, 160, 300), 1);
}

// ====== malformed_offsets ======

#[test]
fn malformed_offsets() {
    let baseline = [432, 120, 240, 400, 100, 50, 160, 300];
    for field in 0..8 {
        for bad in [0, 1, 31, 4096, u32::MAX] {
            let mut args = baseline;
            args[field] = bad;
            assert_eq!(xs_blob_layout(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]), 0);
        }
    }
}

// ====== segment_order_and_size ======

#[test]
fn segment_order_and_size() {
    assert_eq!(xs_blob_layout(432, 240, 120, 400, 100, 50, 160, 300), 0);
    assert_eq!(xs_blob_layout(432, 120, 240, 400, 50, 50, 160, 300), 0);
    assert_eq!(xs_blob_layout(432, 120, 240, 401, 100, 50, 160, 300), 0);
    assert_eq!(xs_blob_layout(433, 120, 240, 400, 100, 50, 160, 300), 0);
}
