use std::mem::{align_of, offset_of, size_of};

pub const SUCCESS: u64 = 0;
pub const INVALID: u64 = (1 << 63) | 2;
pub const UNSUPPORTED: u64 = (1 << 63) | 3;
pub const DEVICE_ERROR: u64 = (1 << 63) | 7;
pub const OUT_OF_RESOURCES: u64 = (1 << 63) | 9;
pub const NX: u64 = 1 << 63;
pub const STACK_BASE: u64 = 0x0000_7000_0000_1000;

// ====== unico proprietario numerico passato al C ====== //

#[repr(C)]
pub struct Tables {
    pub root: u64,
    pub count: u32,
    pub valid: u32,
    pub pages: [u64; 128],
}

impl Default for Tables {
    fn default() -> Self {
        Self {
            root: 0,
            count: 0,
            valid: 0,
            pages: [0; 128],
        }
    }
}

type Allocate = unsafe extern "C" fn(u32, u32, u64, *mut u64) -> u64;
type Release = extern "C" fn(u64, u64) -> u64;

unsafe extern "C" {
    fn paging_test_layout(size: u64, alignment: u64, pages_offset: u64) -> u32;
    fn paging_test_build(
        tables: *mut Tables,
        allocate: Allocate,
        release: Release,
        bits: u32,
        nx: u32,
    ) -> u64;
    fn paging_test_release(tables: *mut Tables, release: Release) -> u64;
}

// ====== adattatori con prestito esclusivo durante la chiamata ====== //

pub fn layout_matches() -> bool {
    // SAFETY: soli valori numerici; il bridge confronta il layout C reale.
    unsafe {
        paging_test_layout(
            size_of::<Tables>() as u64,
            align_of::<Tables>() as u64,
            offset_of!(Tables, pages) as u64,
        ) == 1
    }
}

pub fn build(tables: &mut Tables, bits: u32, nx: u32) -> u64 {
    assert!(layout_matches());
    // SAFETY: ABI verificata, proprietario esclusivo e callback sincroni.
    // Il modello mantiene vive le pagine per tutta la chiamata C.
    unsafe {
        paging_test_build(
            tables,
            super::firmware::allocate,
            super::firmware::release,
            bits,
            nx,
        )
    }
}

pub fn release(tables: &mut Tables) -> u64 {
    assert!(layout_matches());
    // SAFETY: stesso proprietario e stesso modello usati nella costruzione.
    unsafe { paging_test_release(tables, super::firmware::release) }
}
