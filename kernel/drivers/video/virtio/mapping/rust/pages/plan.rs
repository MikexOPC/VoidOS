#![forbid(unsafe_code)]

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Ranges {
    pub address: [u64; 4],
    pub length: [u64; 4],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Plan {
    pub physical: [u64; 16],
    pub offset: [u64; 4],
    pub count: u32,
    pub valid: u32,
}

// ====== build ======

pub fn build(ranges: Ranges, bits: u32) -> Plan {
    let mut plan = Plan::default();
    if !(32..=52).contains(&bits) { return plan; }
    if ranges.address == [0;4] && ranges.length == [0;4] {
        plan.valid=1;
        return plan;
    }
    let limit=1u64 << bits;
    for (&address,&length) in ranges.address.iter().zip(ranges.length.iter()) {
        if address==0 || length==0 || address>=limit { return Plan::default(); }
        let Some(end)=address.checked_add(length) else { return Plan::default(); };
        if end>limit { return Plan::default(); }
        let first=address & !4095;
        let last=end.saturating_sub(1) & !4095;
        let Some(span)=last.checked_sub(first) else { return Plan::default(); };
        if span/4096 >= 16 { return Plan::default(); }
        let mut physical=first;
        loop {
            if !plan.physical.iter().take(plan.count as usize).any(|v| *v==physical) {
                let Some(slot)=plan.physical.get_mut(plan.count as usize) else { return Plan::default(); };
                *slot=physical;
                plan.count=plan.count.saturating_add(1);
            }
            if physical==last { break; }
            physical=physical.saturating_add(4096);
        }
    }
    for index in 0..plan.count as usize {
        for other in index.saturating_add(1)..plan.count as usize {
            let (Some(&first),Some(&second))=(plan.physical.get(index),plan.physical.get(other)) else { return Plan::default(); };
            if first>second {
                if let Some(slot)=plan.physical.get_mut(index) { *slot=second; }
                if let Some(slot)=plan.physical.get_mut(other) { *slot=first; }
            }
        }
    }
    for (index,&address) in ranges.address.iter().enumerate() {
        let mut page=None;
        for candidate in 0..16 {
            if candidate>=plan.count as usize { break; }
            if plan.physical.get(candidate)==Some(&(address & !4095)) { page=Some(candidate); break; }
        }
        let Some(page)=page else { return Plan::default(); };
        let Some(slot)=plan.offset.get_mut(index) else { return Plan::default(); };
        *slot=(page as u64).saturating_mul(4096).saturating_add(address & 4095);
    }
    plan.valid=1;
    plan
}

#[cfg(test)]
#[path="../../../../../../../test/kernel/drivers/video/virtio/mapping/rust/pages/plan_test.rs"]
mod tests;
