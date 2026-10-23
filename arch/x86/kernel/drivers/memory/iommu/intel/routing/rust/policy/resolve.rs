#![forbid(unsafe_code)]
#[path="../../../../../../bus/pci/topology/rust/policy/graph.rs"]
pub mod graph;
mod types;
mod validation;
pub use graph::Node;
pub use types::{Info,Unit,Scope,Request};

// ====== encode ======

pub fn encode(unit:u32,kind:u32,scope:u32,node:u32)->u64 {
    u64::from(unit.saturating_add(2)) | (u64::from(kind)<<8) | (u64::from(scope)<<16) | (u64::from(node)<<32)
}

// ====== contains ======

fn contains(nodes:&[Node],ancestor:u32,mut current:u32)->bool {
    for _ in 0..9 {
        if current==ancestor { return true; }
        let Some(node)=nodes.get(current as usize) else { return false; };
        current=node.parent;
    }
    false
}

// ====== walk ======

// Intel VT-d5.0 8.3.1 p8-5/8-6: start bus then each bridge's secondary, not bus arithmetic.
fn walk(info:&Info,nodes:&[Node],scope:&Scope)->Option<u32> {
    let segment=info.unit.get(scope.unit as usize)?.segment;
    let mut bus=scope.bus; let mut parent=graph::NONE; let mut terminal=graph::NONE;
    for step in 0..scope.depth.min(8) as usize {
        let &[device,function]=scope.path.as_chunks::<2>().0.get(step)?;
        let index=nodes.iter().position(|n|n.segment==segment && n.bus==bus && n.device==device && n.function==function)?;
        let node=nodes.get(index)?;
        if node.parent!=parent { return None; }
        terminal=index as u32;
        if step.saturating_add(1)<scope.depth as usize {
            if (node.header>>16)&127!=1 || (node.buses>>8)&255==0 { return None; }
            bus=((node.buses>>8)&255) as u8; parent=terminal;
        }
    }
    let node=nodes.get(terminal as usize)?;
    if (node.header>>16)&127!=u32::from(scope.kind==2) { return None; }
    Some(terminal)
}

// ====== resolve ======

// Threat: malformed/ambiguous firmware selecting the wrong remapper. Reject; never enable DMA here.
pub fn resolve(info:&Info,nodes:&[Node],request:&Request,bits:u32)->u64 {
    if !validation::valid(info,bits) || graph::check(nodes)==0 || request.segment>65535 || request.bus>255 ||
        request.device>31 || request.function>7 { return 0; }
    let Some(target)=nodes.iter().position(|n|u32::from(n.segment)==request.segment && u32::from(n.bus)==request.bus &&
        u32::from(n.device)==request.device && u32::from(n.function)==request.function) else { return 0; };
    let Some(gpu)=nodes.get(target) else { return 0; };
    // Any type-0 endpoint (block 3 routes the edu requester, class FFh); bridges, base class 06h, never request DMA here.
    if gpu.id!=request.id || (gpu.header>>16)&127!=0 || gpu.class>>24==6 { return 0; }
    let mut terms=[graph::NONE;32];
    for index in 0..info.scopes.min(32) as usize {
        let Some(scope)=info.scope.get(index) else { return 0; };
        let Some(unit)=info.unit.get(scope.unit as usize) else { return 0; };
        if u32::from(unit.segment)!=request.segment { continue; }
        for prior in info.scope.iter().take(index) {
            let Some(owner)=info.unit.get(prior.unit as usize) else { return 0; };
            if owner.segment==unit.segment && scope.kind>=3 && prior.kind==scope.kind && prior.enumeration==scope.enumeration { return 0; }
        }
        if scope.kind>=3 { continue; }
        let Some(terminal)=walk(info,nodes,scope) else { return 0; };
        let Some(slot)=terms.get_mut(index) else { return 0; }; *slot=terminal;
        for prior in 0..index {
            let Some(old)=info.scope.get(prior) else { return 0; };
            let Some(&other)=terms.get(prior) else { return 0; };
            if other==graph::NONE { continue; }
            if terminal==other || (old.kind==2 && contains(nodes,other,terminal)) ||
                (scope.kind==2 && contains(nodes,terminal,other)) { return 0; }
        }
    }
    let mut selected=1u64;
    for index in 0..info.scopes.min(32) as usize {
        let Some(scope)=info.scope.get(index) else { return 0; };
        let Some(&terminal)=terms.get(index) else { return 0; };
        if terminal!=graph::NONE && (terminal==target as u32 || (scope.kind==2 && contains(nodes,terminal,target as u32))) {
            if selected!=1 { return 0; }
            selected=encode(u32::from(scope.unit),u32::from(scope.kind),index as u32,target as u32);
        }
    }
    if selected!=1 { return selected; }
    for index in 0..info.units.min(8) as usize {
        let Some(unit)=info.unit.get(index) else { return 0; };
        if u32::from(unit.segment)==request.segment && unit.flags==1 { return encode(index as u32,3,32,target as u32); }
    }
    1
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/routing/rust/policy/resolve_test.rs"]
mod tests;
