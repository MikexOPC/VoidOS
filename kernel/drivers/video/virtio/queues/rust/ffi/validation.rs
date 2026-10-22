#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../policy/validation.rs"]
mod policy;

// ====== kernel_vgpu_queue_register ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_register(offset:u32,width:u32,write:u32)->u32 {
    policy::register(offset,width,write)
}

// ====== kernel_vgpu_queue_size ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_size(maximum:u32)->u32 { policy::size(maximum) }

// ====== kernel_vgpu_queue_completion ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_completion(old:u32,current:u32,id:u32,bytes:u32)->u32 {
    policy::completion(old,current,id,bytes)
}

// ====== kernel_vgpu_queue_display ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_display(x:u32,y:u32,width:u32,height:u32,enabled:u32,flags:u32)->u32 {
    policy::display(x,y,width,height,enabled,flags)
}

// ====== kernel_vgpu_queue_message ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_message(command:u32,bytes:u32)->u32 { policy::message(command,bytes) }

// ====== kernel_vgpu_queue_completion_sized ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_vgpu_queue_completion_sized(old:u32,current:u32,id:u32,bytes:u32,expected:u32)->u32 {
    policy::completion_sized(old,current,id,bytes,expected)
}
