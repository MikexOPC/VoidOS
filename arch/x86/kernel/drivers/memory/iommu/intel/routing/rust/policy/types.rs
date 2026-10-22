#[repr(C)]
#[derive(Clone,Copy,Default,PartialEq,Eq)]
pub struct Unit {
    pub base:u64, pub bytes:u64, pub segment:u16, pub flags:u8, pub first:u8, pub count:u32,
}
#[repr(C)]
#[derive(Clone,Copy,Default,PartialEq,Eq)]
pub struct Scope {
    pub unit:u8, pub kind:u8, pub enumeration:u8, pub bus:u8, pub depth:u8, pub reserved:[u8;3], pub path:[u8;16],
}
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct Info {
    pub physical:u64, pub bits:u32, pub flags:u32, pub units:u32, pub scopes:u32, pub other:u32, pub reserved:u32,
    pub unit:[Unit;8], pub scope:[Scope;32],
}
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct Request { pub segment:u32, pub bus:u32, pub device:u32, pub function:u32, pub id:u32 }
const _:()=assert!(core::mem::size_of::<Unit>()==24 && core::mem::size_of::<Scope>()==24 &&
    core::mem::size_of::<Info>()==992 && core::mem::size_of::<Request>()==20);
