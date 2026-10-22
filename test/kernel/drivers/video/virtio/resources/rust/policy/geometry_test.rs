use super::*;

// ====== geometry ======

#[test]
fn geometry() {
    assert_eq!(surface_bytes(1280,800),4096000);
    assert_eq!(surface_bytes(2048,2048),16777216);
    for (w,h) in [(0,1),(1,0),(4096,4096),(u32::MAX,1)] { assert_eq!(surface_bytes(w,h),0); }
}

// ====== paging ======

#[test]
fn rectangles() {
    assert_eq!(offset(1280,800,16,24,64,32),(24*1280+16)*4);
    assert_eq!(offset(1280,800,1279,799,1,1),4095996);
    for (x,y,w,h) in [(0,0,0,1),(0,0,1,0),(1280,0,1,1),(0,800,1,1),(1279,0,2,1),(0,799,1,2),
        (u32::MAX,0,1,1),(0,0,u32::MAX,1)] { assert_eq!(offset(1280,800,x,y,w,h),u64::MAX); }
    assert_eq!(offset(4096,4096,0,0,1,1),u64::MAX);
}

// ====== paging ======

#[test]
fn paging() {
    for pages in 1u64..=4096 {
        let count=2+(pages+2).div_ceil(512);
        let mut leaves=0;
        for table in 2..count {
            for slot in 0..512 {
                let index=(table-2)*512+slot;
                let value=entry(table as u32,slot as u32,0x100000,pages,0);
                if index>0 && index<=pages { assert_eq!(value,(0x100000+(index-1)*4096)|0x8000000000000003); leaves+=1; }
                else { assert_eq!(value,0); }
            }
        }
        assert_eq!(leaves,pages);
    }
    assert_eq!(entry(0,0,0x100000,1000,0x200000),0x200003);
    assert_eq!(entry(0,1,0x100000,1000,0),0);
    assert_eq!(entry(2,512,0x100000,1000,0),u64::MAX);
    assert_eq!(entry(2,1,u64::MAX,1000,0),u64::MAX);
    assert_eq!(entry(2,1,0x100000,4097,0),u64::MAX);
}

// ====== transitions ======

#[test]
fn transitions() {
    let commands=[0x101,0x106,0x105,0x107,0x102];
    for state in 0..7 {
        for (index,cmd) in commands.iter().enumerate() {
            assert_eq!(transition(state,*cmd),if state==index as u32 {state+1} else {u32::MAX});
        }
    }
}
