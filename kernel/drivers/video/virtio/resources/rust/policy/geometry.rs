#![forbid(unsafe_code)]

// ====== surface_bytes ======

pub fn surface_bytes(width:u32,height:u32)->u64 {
    if width==0 || height==0 || width>16384 || height>16384 { return 0; }
    let bytes=u64::from(width)*u64::from(height)*4;
    if bytes>16*1024*1024 { 0 } else { bytes }
}

// ====== offset ======

pub fn offset(width:u32,height:u32,x:u32,y:u32,w:u32,h:u32)->u64 {
    if surface_bytes(width,height)==0 || w==0 || h==0 || x>=width || y>=height || w>width-x || h>height-y {
        return u64::MAX;
    }
    u64::from(y).checked_mul(u64::from(width)).and_then(|row| row.checked_add(u64::from(x)))
        .and_then(|pixel| pixel.checked_mul(4)).unwrap_or(u64::MAX)
}

// ====== entry ======

pub fn entry(table:u32,slot:u32,base:u64,pages:u64,child:u64)->u64 {
    if slot>=512 || pages==0 || pages>4096 || base<4096 || base&4095!=0 || base> (1u64<<52)-pages*4096 {
        return u64::MAX;
    }
    let count=2+(pages+2).div_ceil(512);
    if u64::from(table)>=count { return u64::MAX; }
    if table<2 {
        let present=(table==0 && slot==0) || (table==1 && u64::from(slot)<count-2);
        if !present { return 0; }
        if child<4096 || child&4095!=0 || child>=1u64<<52 { return u64::MAX; }
        return child|3;
    }
    let index=u64::from(table-2)*512+u64::from(slot);
    if index==0 || index>pages { 0 } else { (base+(index-1)*4096)|0x8000000000000003 }
}

// ====== transition ======

pub fn transition(state:u32,command:u32)->u32 {
    match (state,command) {
        (0,0x101)=>1, (1,0x106)=>2, (2,0x105)=>3, (3,0x107)=>4, (4,0x102)=>5,
        _=>u32::MAX,
    }
}

#[cfg(test)]
#[path="../../../../../../../test/kernel/drivers/video/virtio/resources/rust/policy/geometry_test.rs"]
mod tests;
