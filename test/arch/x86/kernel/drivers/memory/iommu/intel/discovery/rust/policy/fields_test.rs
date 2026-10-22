use super::*;

// ====== header_fields ======

#[test]
fn header_fields() {
    assert_eq!(header(39,1,1),40);
    assert_eq!(header(63,7,1),64);
    for (w,f,r) in [(30,0,1),(64,0,1),(39,8,1),(39,0,2)] { assert_eq!(header(w,f,r),0); }
}

// ====== register_ranges ======

#[test]
fn register_ranges() {
    for size in 0..16 {
        let span=4096u64<<size;
        assert_eq!(register_span(0x80000000,size,40),span);
        assert_eq!(register_span(0,size,40),0);
        assert_eq!(register_span(0x80000001,size,40),0);
    }
    assert_eq!(register_span(u64::MAX,0,52),0);
    assert_eq!(register_span(0x100000,16,40),0);
    assert_eq!(register_span(0x100000,0,53),0);
}

// ====== scopes ======

#[test]
fn scopes() {
    for kind in 1..=5 { assert_eq!(scope_fields(kind,8,0,0,0),1); }
    assert_eq!(scope_fields(1,22,0,0,0),8);
    for (t,l,f,r,e) in [(0,8,0,0,0),(6,8,0,0,0),(1,7,0,0,0),(1,24,0,0,0),(1,9,0,0,0),
        (1,8,1,0,0),(1,8,0,1,0),(1,8,0,0,1),(4,10,0,0,0)] { assert_eq!(scope_fields(t,l,f,r,e),0); }
}
