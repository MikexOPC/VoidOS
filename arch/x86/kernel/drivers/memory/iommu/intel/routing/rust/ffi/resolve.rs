#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../policy/resolve.rs"]
pub mod policy;
use policy::{Info,Node,Request};

// ====== pointer_valid ======

fn pointer_valid<T>(value:*const T,count:usize)->bool {
    !value.is_null() && (value as usize).is_multiple_of(core::mem::align_of::<T>()) &&
        count.checked_mul(core::mem::size_of::<T>()).and_then(|bytes|(value as usize).checked_add(bytes)).is_some()
}

// ====== x86_vtd_route_word ======

/// # Safety
/// C provides initialized, mapped Info/Request and count Nodes, all immutable and live until return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_vtd_route_word(info:*const Info,nodes:*const Node,count:u32,request:*const Request,bits:u32)->u64 {
    if count>512 || !pointer_valid(info,1) || !pointer_valid(nodes,count as usize) || !pointer_valid(request,1) { return 0; }
    // Shared borrows do not escape this call; C owns the buffers and prevents concurrent mutation.
    let (info,entries,request)=unsafe { (&*info,core::slice::from_raw_parts(nodes,count as usize),&*request) };
    policy::resolve(info,entries,request,bits)
}
