use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::ptr;
use core::sync::atomic::{AtomicBool, Ordering};

const MIN_ALIGN: usize = 16;
const MAGIC_FREE: u32 = 0x4652_4545;
const MAGIC_USED: u32 = 0x5553_4544;

#[repr(C, align(16))]
struct BlockHeader {
    size: usize,
    magic: u32,
    _pad: u32,
    next: *mut BlockHeader,
}

const HEADER_SIZE: usize = core::mem::size_of::<BlockHeader>();

// ====== align_up ======

#[inline]
const fn align_up(val: usize, align: usize) -> usize {
    (val + align - 1) & !(align - 1)
}

// ====== HeapCore ======

pub struct HeapCore {
    base: usize,
    size: usize,
    head: *mut BlockHeader,
    initialized: bool,
}

impl HeapCore {
    pub const fn empty() -> Self {
        Self {
            base: 0,
            size: 0,
            head: ptr::null_mut(),
            initialized: false,
        }
    }

    pub unsafe fn init(&mut self, base: usize, size: usize) -> bool {
        if self.initialized || base == 0 || size < HEADER_SIZE * 2 {
            return false;
        }
        let aligned = align_up(base, MIN_ALIGN);
        let lost = aligned.saturating_sub(base);
        if size <= lost + HEADER_SIZE * 2 {
            return false;
        }
        let usable = (size - lost) & !(MIN_ALIGN - 1);
        let first = aligned as *mut BlockHeader;
        unsafe {
            (*first).size = usable;
            (*first).magic = MAGIC_FREE;
            (*first)._pad = 0;
            (*first).next = ptr::null_mut();
        }
        self.base = aligned;
        self.size = usable;
        self.head = first;
        self.initialized = true;
        true
    }

    pub unsafe fn alloc(&mut self, layout: Layout) -> *mut u8 {
        if !self.initialized || layout.size() == 0 {
            return ptr::null_mut();
        }
        let align = layout.align().max(MIN_ALIGN);
        let req_size = align_up(layout.size(), MIN_ALIGN);

        let mut prev: *mut BlockHeader = ptr::null_mut();
        let mut cur = self.head;

        while !cur.is_null() {
            let cur_addr = cur as usize;
            let cur_size = unsafe { (*cur).size };
            let payload_start = align_up(cur_addr + HEADER_SIZE, align);
            let total_needed = match payload_start
                .checked_sub(cur_addr)
                .and_then(|lead| lead.checked_add(req_size))
            {
                Some(n) => align_up(n, MIN_ALIGN),
                None => return ptr::null_mut(),
            };

            if cur_size >= total_needed {
                let next_free = unsafe { (*cur).next };
                let prefix_bytes = payload_start - HEADER_SIZE - cur_addr;

                // Split off an aligned prefix block if alignment padding is large enough.
                let (alloc_blk, alloc_total) = if prefix_bytes >= HEADER_SIZE + MIN_ALIGN {
                    let prefix_blk = cur;
                    let aligned_blk = (payload_start - HEADER_SIZE) as *mut BlockHeader;
                    let rem = cur_size - prefix_bytes;
                    unsafe {
                        (*prefix_blk).size = prefix_bytes;
                        (*prefix_blk).magic = MAGIC_FREE;
                        (*prefix_blk).next = aligned_blk;
                        (*aligned_blk).size = rem;
                        (*aligned_blk).magic = MAGIC_FREE;
                        (*aligned_blk).next = next_free;
                    }
                    prev = prefix_blk;
                    (aligned_blk, rem)
                } else {
                    (cur, cur_size)
                };

                let used_size = align_up(
                    (payload_start - alloc_blk as usize) + req_size,
                    MIN_ALIGN,
                );
                let tail_rem = alloc_total - used_size;
                let replacement = if tail_rem >= HEADER_SIZE + MIN_ALIGN {
                    let tail = ((alloc_blk as usize) + used_size) as *mut BlockHeader;
                    unsafe {
                        (*tail).size = tail_rem;
                        (*tail).magic = MAGIC_FREE;
                        (*tail)._pad = 0;
                        (*tail).next = next_free;
                        (*alloc_blk).size = used_size;
                    }
                    tail
                } else {
                    unsafe {
                        (*alloc_blk).size = alloc_total;
                    }
                    next_free
                };

                unsafe {
                    (*alloc_blk).magic = MAGIC_USED;
                    (*alloc_blk).next = ptr::null_mut();
                }
                if prev.is_null() {
                    self.head = replacement;
                } else {
                    unsafe {
                        (*prev).next = replacement;
                    }
                }
                // Store back-offset to BlockHeader right before payload when padded.
                if payload_start != (alloc_blk as usize) + HEADER_SIZE {
                    let back_ptr = (payload_start - core::mem::size_of::<usize>()) as *mut usize;
                    unsafe {
                        *back_ptr = alloc_blk as usize;
                    }
                }
                return payload_start as *mut u8;
            }
            prev = cur;
            cur = unsafe { (*cur).next };
        }
        ptr::null_mut()
    }

    pub unsafe fn dealloc(&mut self, ptr: *mut u8, layout: Layout) {
        if !self.initialized || ptr.is_null() {
            return;
        }
        let align = layout.align().max(MIN_ALIGN);
        let addr = ptr as usize;
        let blk = if align <= MIN_ALIGN {
            (addr - HEADER_SIZE) as *mut BlockHeader
        } else {
            let candidate = (addr - HEADER_SIZE) as *mut BlockHeader;
            if (candidate as usize) >= self.base && unsafe { (*candidate).magic == MAGIC_USED } {
                candidate
            } else {
                let back_ptr = (addr - core::mem::size_of::<usize>()) as *const usize;
                unsafe { (*back_ptr) as *mut BlockHeader }
            }
        };
        if (blk as usize) < self.base || (blk as usize) >= self.base + self.size {
            return;
        }
        unsafe {
            if (*blk).magic != MAGIC_USED {
                return;
            }
            (*blk).magic = MAGIC_FREE;
        }

        // Insert into address-ordered free list and coalesce adjacent blocks.
        let mut prev: *mut BlockHeader = ptr::null_mut();
        let mut cur = self.head;
        while !cur.is_null() && (cur as usize) < (blk as usize) {
            prev = cur;
            cur = unsafe { (*cur).next };
        }
        unsafe {
            (*blk).next = cur;
        }
        if prev.is_null() {
            self.head = blk;
        } else {
            unsafe {
                (*prev).next = blk;
            }
        }

        // Coalesce with next block.
        if !cur.is_null() && (blk as usize) + unsafe { (*blk).size } == (cur as usize) {
            unsafe {
                (*blk).size += (*cur).size;
                (*blk).next = (*cur).next;
            }
        }
        // Coalesce with previous block.
        if !prev.is_null() && (prev as usize) + unsafe { (*prev).size } == (blk as usize) {
            unsafe {
                (*prev).size += (*blk).size;
                (*prev).next = (*blk).next;
            }
        }
    }

    pub fn free_bytes(&self) -> usize {
        let mut total = 0usize;
        let mut cur = self.head;
        while !cur.is_null() {
            total += unsafe { (*cur).size };
            cur = unsafe { (*cur).next };
        }
        total
    }
}

// ====== ShmAllocator (Ring 3 GlobalAlloc backed by SYS_SHM_CREATE + SYS_SHM_MAP) ======

pub struct ShmAllocator {
    pages: u64,
    lock: AtomicBool,
    core: UnsafeCell<HeapCore>,
}

unsafe impl Sync for ShmAllocator {}

impl ShmAllocator {
    pub const fn new(pages: u64) -> Self {
        Self {
            pages,
            lock: AtomicBool::new(false),
            core: UnsafeCell::new(HeapCore::empty()),
        }
    }

    fn lock(&self) {
        while self
            .lock
            .compare_exchange_weak(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }

    fn unlock(&self) {
        self.lock.store(false, Ordering::Release);
    }

    #[cfg(not(test))]
    unsafe fn ensure_init(&self, heap: &mut HeapCore) -> bool {
        if heap.initialized {
            return true;
        }
        let shm_id = crate::shm_create(self.pages);
        if shm_id <= 0 {
            return false;
        }
        let virt = crate::shm_map(shm_id as u64);
        if virt < 0x1000_0000 {
            return false;
        }
        unsafe { heap.init(virt as usize, (self.pages as usize) * 4096) }
    }

    #[cfg(test)]
    unsafe fn ensure_init(&self, heap: &mut HeapCore) -> bool {
        let _ = self.pages;
        heap.initialized
    }
}

unsafe impl GlobalAlloc for ShmAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        self.lock();
        let heap = unsafe { &mut *self.core.get() };
        let ptr = if unsafe { self.ensure_init(heap) } {
            unsafe { heap.alloc(layout) }
        } else {
            ptr::null_mut()
        };
        self.unlock();
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        self.lock();
        let heap = unsafe { &mut *self.core.get() };
        unsafe {
            heap.dealloc(ptr, layout);
        }
        self.unlock();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alloc_dealloc_alignment_and_coalescing() {
        let mut backing = vec![0u8; 64 * 1024];
        let mut heap = HeapCore::empty();
        assert!(unsafe { heap.init(backing.as_mut_ptr() as usize, backing.len()) });
        let initial_free = heap.free_bytes();

        let l1 = Layout::from_size_align(64, 16).unwrap();
        let l2 = Layout::from_size_align(512, 64).unwrap();
        let l3 = Layout::from_size_align(4096, 4096).unwrap();

        let p1 = unsafe { heap.alloc(l1) };
        let p2 = unsafe { heap.alloc(l2) };
        let p3 = unsafe { heap.alloc(l3) };
        assert!(!p1.is_null() && (p1 as usize) % 16 == 0);
        assert!(!p2.is_null() && (p2 as usize) % 64 == 0);
        assert!(!p3.is_null() && (p3 as usize) % 4096 == 0);

        // OOM returns null cleanly.
        let huge = Layout::from_size_align(128 * 1024, 16).unwrap();
        assert!(unsafe { heap.alloc(huge) }.is_null());

        // Dealloc in non-FIFO order and verify full coalescing.
        unsafe {
            heap.dealloc(p2, l2);
            heap.dealloc(p1, l1);
            heap.dealloc(p3, l3);
        }
        assert_eq!(heap.free_bytes(), initial_free);
    }
}
