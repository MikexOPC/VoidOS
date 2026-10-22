#![forbid(unsafe_code)]
pub const NONE:u32=u32::MAX;
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct Node {
    pub id:u32, pub class:u32, pub header:u32, pub buses:u32,
    pub segment:u16, pub bus:u8, pub device:u8, pub function:u8, pub reserved:[u8;3],
    pub parent:u32, pub depth:u32,
}
const _:()=assert!(core::mem::size_of::<Node>()==32);

// ====== bridge and active ======

fn bridge(node:&Node)->bool { (node.header>>16)&127==1 }
fn active(node:&Node)->bool { bridge(node) && (node.buses>>8)&255!=0 }

// ====== fields ======

fn fields(nodes:&[Node])->bool {
    if nodes.len()>512 { return false; }
    let mut bridges=0u32;
    for (index,node) in nodes.iter().enumerate() {
        let kind=(node.header>>16)&127;
        if node.id&65535<=1 || node.id&65535==65535 || node.device>31 || node.function>7 || node.reserved!=[0;3] || kind>1 { return false; }
        if kind==1 {
            bridges=bridges.saturating_add(1);
            let primary=node.buses&255; let secondary=(node.buses>>8)&255; let subordinate=(node.buses>>16)&255;
            if node.class>>16!=0x0604 || bridges>64 { return false; }
            if secondary==0 && subordinate==0 {
                if primary!=0 && primary!=u32::from(node.bus) { return false; }
            } else if primary!=u32::from(node.bus) || secondary==0 || secondary>subordinate || secondary==primary { return false; }
        } else if node.buses!=0 || node.class>>16==0x0604 { return false; }
        for prior in nodes.iter().take(index) {
            if (node.segment,node.bus,node.device,node.function)==(prior.segment,prior.bus,prior.device,prior.function) { return false; }
        }
        if node.function!=0 && !nodes.iter().any(|zero| zero.segment==node.segment && zero.bus==node.bus &&
            zero.device==node.device && zero.function==0 && zero.header&(1<<23)!=0) { return false; }
    }
    true
}

// ====== parent ======

fn parent(nodes:&[Node],node:&Node)->Option<u32> {
    let mut found=NONE;
    for (index,candidate) in nodes.iter().enumerate() {
        if active(candidate) && candidate.segment==node.segment && ((candidate.buses>>8)&255)==u32::from(node.bus) {
            if found!=NONE { return None; }
            found=index as u32;
        }
    }
    Some(found)
}

// ====== depth ======

fn depth(nodes:&[Node],node:&Node)->Option<u32> {
    let mut current=node.parent; let mut count=0u32;
    while current!=NONE {
        let ancestor=nodes.get(current as usize)?;
        count=count.checked_add(1)?;
        if count>8 || !active(ancestor) || ancestor.segment!=node.segment { return None; }
        let first=(ancestor.buses>>8)&255; let last=(ancestor.buses>>16)&255;
        if u32::from(node.bus)<first || u32::from(node.bus)>last { return None; }
        if active(node) && ((node.buses>>8)&255<first || (node.buses>>16)&255>last) { return None; }
        current=ancestor.parent;
    }
    Some(count)
}

// ====== ancestor ======

fn ancestor(nodes:&[Node],index:usize,node:&Node)->bool {
    let mut next=node.parent;
    for _ in 0..8 {
        if next==NONE { return false; }
        if next as usize==index { return true; }
        let Some(entry)=nodes.get(next as usize) else { return false; };
        next=entry.parent;
    }
    false
}

// ====== relations ======

fn relations(nodes:&[Node],check_depth:bool)->u32 {
    let mut bridges=0u32;
    for (index,node) in nodes.iter().enumerate() {
        if parent(nodes,node)!=Some(node.parent) { return 0; }
        let Some(value)=depth(nodes,node) else { return 0; };
        if check_depth && value!=node.depth { return 0; }
        if bridge(node) { bridges=bridges.saturating_add(1); }
        for (other,candidate) in nodes.iter().enumerate() {
            if !active(candidate) || candidate.segment!=node.segment { continue; }
            let first=(candidate.buses>>8)&255; let last=(candidate.buses>>16)&255;
            if node.parent==NONE && first<=u32::from(node.bus) && u32::from(node.bus)<=last { return 0; }
            if other>=index || !active(node) { continue; }
            let start=(node.buses>>8)&255; let end=(node.buses>>16)&255;
            if start<=last && first<=end && !ancestor(nodes,other,node) && !ancestor(nodes,index,candidate) { return 0; }
        }
    }
    bridges.saturating_add(1)
}

// ====== build ======

pub fn build(nodes:&mut [Node])->u32 {
    if !fields(nodes) { return 0; }
    for index in 0..nodes.len() {
        let Some(node)=nodes.get(index) else { return 0; };
        let Some(value)=parent(nodes,node) else { return 0; };
        let Some(target)=nodes.get_mut(index) else { return 0; };
        target.parent=value;
    }
    let result=relations(nodes,false);
    if result==0 { return 0; }
    for index in 0..nodes.len() {
        let Some(node)=nodes.get(index) else { return 0; };
        let Some(value)=depth(nodes,node) else { return 0; };
        let Some(target)=nodes.get_mut(index) else { return 0; };
        target.depth=value;
    }
    result
}

// ====== check ======

pub fn check(nodes:&[Node])->u32 { if fields(nodes) { relations(nodes,true) } else { 0 } }

#[cfg(test)]
#[path="../../../../../../../../../test/arch/x86/kernel/drivers/bus/pci/topology/rust/policy/graph_test.rs"]
mod tests;
