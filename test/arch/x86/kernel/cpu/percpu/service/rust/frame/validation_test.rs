use super::idle_return_valid;

// ====== ordinary_and_diagnostic ======

#[test]
fn ordinary_and_diagnostic() {
    for top in [0x18000u64, 0xffff_9000_1234_0000, u64::MAX] {
        let rsp = top - 576;
        let frame = (rsp & !15) - 48;
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x202,16,0),1);
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x602,16,1),1);
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x602,16,0),0);
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x202,16,1),0);
        for invalid in [2,u32::MAX] {
            assert_eq!(idle_return_valid(frame,top,rsp,8,0x202,16,invalid),0);
        }
    }
}

// ====== exact_frame_geometry ======

#[test]
fn exact_frame_geometry() {
    for alignment in 0..16 {
        let top = 0x18000u64 + alignment;
        let rsp = top - 576;
        let frame = (rsp & !15) - 48;
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x202,16,0),1);
        for delta in 1..128 {
            assert_eq!(idle_return_valid(frame + delta,top,rsp,8,0x202,16,0),0);
            assert_eq!(idle_return_valid(frame - delta,top,rsp,8,0x202,16,0),0);
            assert_eq!(idle_return_valid(frame,top,rsp + delta,8,0x202,16,0),0);
            assert_eq!(idle_return_valid(frame,top,rsp - delta,8,0x202,16,0),0);
        }
    }
    for top in 0..624 {
        assert_eq!(idle_return_valid(0,top,0,8,0x202,16,0),0);
    }
}

// ====== selectors_and_flag_matrix ======

#[test]
fn selectors_and_flag_matrix() {
    let top = 0x18000;
    let rsp = top - 576;
    let frame = rsp - 48;
    for selector in 0..256 {
        assert_eq!(idle_return_valid(frame,top,rsp,selector,0x202,16,0),u32::from(selector == 8));
        assert_eq!(idle_return_valid(frame,top,rsp,8,0x202,selector,0),u32::from(selector == 16));
    }
    for flags in 0..65536 {
        for diagnostic in 0..2 {
            let expected = if diagnostic == 0 {0x202} else {0x602};
            assert_eq!(idle_return_valid(frame,top,rsp,8,flags,16,diagnostic),
                u32::from(flags & 0x7602 == expected));
        }
    }
}
