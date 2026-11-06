use core::ops::{Deref, DerefMut};
use core::ptr::write_volatile;
use core::sync::atomic::{Ordering, compiler_fence};

// ====== Wipe ======

pub trait Wipe {
    fn wipe_in_place(&mut self);
}

macro_rules! wipe_integer {
    ($($kind:ty),*) => {
        $(
            impl Wipe for $kind {
                fn wipe_in_place(&mut self) {
                    unsafe { write_volatile(self, 0) }
                }
            }
        )*
    };
}

wipe_integer!(u8, u16, u32, u64, u128, usize);

impl<T: Wipe> Wipe for [T] {
    fn wipe_in_place(&mut self) {
        for item in self.iter_mut() {
            item.wipe_in_place();
        }
    }
}

impl<T: Wipe, const N: usize> Wipe for [T; N] {
    fn wipe_in_place(&mut self) {
        self.as_mut_slice().wipe_in_place();
    }
}

// ====== wipe ======

pub fn wipe<T: Wipe + ?Sized>(value: &mut T) {
    value.wipe_in_place();
    compiler_fence(Ordering::SeqCst);
}

// ====== Wiping ======

pub struct Wiping<T: Wipe>(T);

impl<T: Wipe> Wiping<T> {
    pub const fn new(value: T) -> Self {
        Self(value)
    }
}

impl<T: Wipe> Deref for Wiping<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.0
    }
}

impl<T: Wipe> DerefMut for Wiping<T> {
    fn deref_mut(&mut self) -> &mut T {
        &mut self.0
    }
}

impl<T: Wipe> Drop for Wiping<T> {
    fn drop(&mut self) {
        wipe(&mut self.0);
    }
}
