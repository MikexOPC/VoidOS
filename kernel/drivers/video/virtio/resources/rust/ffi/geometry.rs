#![no_std]
#[path="../policy/geometry.rs"]
mod policy;

// ====== kernel_vgpu_surface_bytes ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_surface_bytes(width:u32,height:u32)->u64 { policy::surface_bytes(width,height) }

// ====== kernel_vgpu_surface_offset ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_surface_offset(width:u32,height:u32,x:u32,y:u32,w:u32,h:u32)->u64 {
    policy::offset(width,height,x,y,w,h)
}

// ====== kernel_vgpu_surface_entry ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_surface_entry(table:u32,slot:u32,base:u64,pages:u64,child:u64)->u64 {
    policy::entry(table,slot,base,pages,child)
}

// ====== kernel_vgpu_resource_transition ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_resource_transition(state:u32,command:u32)->u32 { policy::transition(state,command) }
