use super::ffi::{DEVICE_ERROR, INVALID, OUT_OF_RESOURCES, SUCCESS};
use std::alloc::{Layout, alloc, dealloc};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::ptr::NonNull;

// ====== memoria del test con sentinelle prima e dopo ogni pagina ====== //

#[repr(C, align(4096))]
struct GuardedPage([u8; 12288]);

struct Page {
    allocation: NonNull<u8>,
}

impl Page {
    fn new() -> Option<Self> {
        // SAFETY: layout non nullo, memoria inizializzata prima di ogni lettura.
        let allocation = NonNull::new(unsafe { alloc(Layout::new::<GuardedPage>()) })?;
        unsafe { allocation.as_ptr().write_bytes(0xa5, 12288) };
        Some(Self { allocation })
    }

    fn address(&self) -> u64 {
        // SAFETY: pagina centrale interna all'allocazione viva di 12288 byte.
        unsafe { self.allocation.as_ptr().add(4096) as u64 }
    }

    fn guards_intact(&self) -> bool {
        // SAFETY: tutti i byte sono inizializzati e la pagina resta posseduta.
        let bytes = unsafe { std::slice::from_raw_parts(self.allocation.as_ptr(), 12288) };
        bytes[..4096]
            .iter()
            .chain(bytes[8192..].iter())
            .all(|byte| *byte == 0xa5)
    }

    fn read(&self, slot: usize) -> Option<u64> {
        if slot >= 512 {
            return None;
        }
        // SAFETY: indirizzo allineato, slot limitato alla pagina centrale viva.
        Some(unsafe {
            (self.allocation.as_ptr().add(4096) as *const u64)
                .add(slot)
                .read()
        })
    }
}

impl Drop for Page {
    fn drop(&mut self) {
        // SAFETY: unico proprietario; stesso layout usato da alloc.
        unsafe { dealloc(self.allocation.as_ptr(), Layout::new::<GuardedPage>()) };
    }
}

// ====== modello indipendente delle allocazioni ancora vive ====== //

#[derive(Default)]
pub struct Firmware {
    live: BTreeMap<u64, Page>,
    retired: Vec<Page>,
    pub allocations: usize,
    pub release_attempts: usize,
    pub released: usize,
    pub fail_allocation: Option<usize>,
    pub fail_release: BTreeSet<u64>,
    pub invalid_calls: usize,
    pub duplicate_output: bool,
}

thread_local! {
    static STATE: RefCell<Firmware> = RefCell::new(Firmware::default());
}

pub fn reset() {
    STATE.with(|state| *state.borrow_mut() = Firmware::default());
}

pub fn with<R>(operation: impl FnOnce(&mut Firmware) -> R) -> R {
    STATE.with(|state| operation(&mut state.borrow_mut()))
}

impl Firmware {
    pub fn addresses(&self) -> BTreeSet<u64> {
        self.live.keys().copied().collect()
    }

    pub fn guards_intact(&self) -> bool {
        self.live
            .values()
            .chain(self.retired.iter())
            .all(Page::guards_intact)
    }

    pub fn entry(&self, address: u64, slot: usize) -> Option<u64> {
        self.live.get(&address)?.read(slot)
    }
}

// ====== callback ABI C senza assert o panic che attraversano il confine ====== //

pub unsafe extern "C" fn allocate(mode: u32, memory: u32, pages: u64, output: *mut u64) -> u64 {
    STATE
        .try_with(|state| {
            let Ok(mut model) = state.try_borrow_mut() else {
                return DEVICE_ERROR;
            };
            if mode != 0 || memory != 2 || pages != 1 || output.is_null() {
                model.invalid_calls += 1;
                return INVALID;
            }
            if model.fail_allocation == Some(model.allocations) {
                // SAFETY: output valido per un UINT64 nel contratto del callback C.
                // Un fallimento non trasferisce proprieta, anche con output alterato.
                unsafe { output.write(0xdead_beef) };
                return OUT_OF_RESOURCES;
            }
            if model.duplicate_output {
                if let Some(address) = model.live.keys().next().copied() {
                    unsafe { output.write(address) };
                    return SUCCESS;
                }
            }
            let Some(page) = Page::new() else {
                return OUT_OF_RESOURCES;
            };
            let address = page.address();
            model.live.insert(address, page);
            model.allocations += 1;
            unsafe { output.write(address) };
            SUCCESS
        })
        .unwrap_or(DEVICE_ERROR)
}

pub extern "C" fn release(address: u64, pages: u64) -> u64 {
    STATE
        .try_with(|state| {
            let Ok(mut model) = state.try_borrow_mut() else {
                return DEVICE_ERROR;
            };
            model.release_attempts += 1;
            if pages != 1 || !model.live.contains_key(&address) {
                model.invalid_calls += 1;
                return INVALID;
            }
            if model.fail_release.contains(&address) {
                return DEVICE_ERROR;
            }
            if let Some(page) = model.live.remove(&address) {
                // Mantiene i byte per controllare le sentinelle anche dopo il rilascio logico.
                model.retired.push(page);
                model.released += 1;
            }
            SUCCESS
        })
        .unwrap_or(DEVICE_ERROR)
}
