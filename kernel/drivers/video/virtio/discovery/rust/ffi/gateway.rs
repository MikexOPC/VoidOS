#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path = "../pci/validation.rs"]
mod validation;

// ====== kernel_vgpu_pci_bar ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_pci_bar(low: u32, high: u32, size: u64, bits: u32) -> u64 {
    validation::bar(low,high,size,bits)
}

// ====== kernel_vgpu_pci_region ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_pci_region(kind: u32, offset: u32, length: u32,
    multiplier: u32, base: u64, size: u64) -> u64 {
    validation::region(kind,offset,length,multiplier,base,size)
}
