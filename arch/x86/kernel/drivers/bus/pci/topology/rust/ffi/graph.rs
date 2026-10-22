#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../policy/graph.rs"]
mod policy;
use policy::Node;

// ====== pointer_valid ======

fn pointer_valid(nodes:*const Node,count:u32)->bool {
    !nodes.is_null() && (nodes as usize).is_multiple_of(core::mem::align_of::<Node>()) && count<=512 &&
        (nodes as usize).checked_add(count as usize*core::mem::size_of::<Node>()).is_some()
}

// ====== x86_pci_graph_build ======

/// # Safety
/// The caller owns count initialized nodes exclusively until return; the buffer remains mapped and live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_pci_graph_build(nodes:*mut Node,count:u32)->u32 {
    if !pointer_valid(nodes,count) { return 0; }
    // C owns a disjoint live staging array of at least count nodes; no concurrent access during this call.
    let entries=unsafe { core::slice::from_raw_parts_mut(nodes,count as usize) };
    policy::build(entries)
}

// ====== x86_pci_graph_check ======

/// # Safety
/// The caller provides count initialized, readable nodes with no concurrent mutation until return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_pci_graph_check(nodes:*const Node,count:u32)->u32 {
    if !pointer_valid(nodes,count) { return 0; }
    // C provides count initialized nodes; the shared slice does not outlive this call.
    let entries=unsafe { core::slice::from_raw_parts(nodes,count as usize) };
    policy::check(entries)
}
