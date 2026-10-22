use super::*;

// ====== valid ======

// Intel VT-d5.0 8.3/8.3.1: unit flags bit0, segment, contiguous scope ownership, path pairs.
pub fn valid(info:&Info,bits:u32)->bool {
    if !(32..=52).contains(&bits) || !(32..=64).contains(&info.bits) || info.flags&!7!=0 ||
        !(1..=8).contains(&info.units) || info.scopes>32 || info.other!=0 || info.reserved!=0 { return false; }
    let limit=1u64<<bits; let mut end=0u32;
    for index in 0..8 {
        let Some(unit)=info.unit.get(index) else { return false; };
        if index>=info.units as usize { if *unit!=Unit::default() { return false; } continue; }
        if unit.flags>1 || !unit.bytes.is_power_of_two() || !(4096..=1<<27).contains(&unit.bytes) ||
            unit.base<unit.bytes || unit.base&(unit.bytes-1)!=0 || unit.base.checked_add(unit.bytes).is_none_or(|n|n>limit) ||
            u32::from(unit.first)!=end || (unit.count==0 && unit.flags==0) { return false; }
        let Some(next)=end.checked_add(unit.count) else { return false; };
        if next>info.scopes { return false; }
        for prior in info.unit.iter().take(index) {
            if prior.base.abs_diff(unit.base)<if prior.base<=unit.base {prior.bytes}else{unit.bytes} ||
                (prior.segment==unit.segment && prior.flags==1) { return false; }
        }
        for scope in info.scope.iter().skip(end as usize).take(unit.count as usize) {
            if scope.unit as usize!=index || !(1..=4).contains(&scope.kind) || !(1..=8).contains(&scope.depth) ||
                scope.reserved!=[0;3] || (scope.kind<=2 && (scope.enumeration!=0 || unit.flags!=0)) ||
                (scope.kind==4 && scope.depth!=1) { return false; }
            for pair in 0..8 {
                let Some(&[device,function])=scope.path.as_chunks::<2>().0.get(pair) else { return false; };
                if pair<scope.depth as usize {
                    if device>31 || function>7 { return false; }
                } else if device!=0 || function!=0 { return false; }
            }
        }
        end=next;
    }
    if end!=info.scopes { return false; }
    info.scope.iter().skip(info.scopes as usize).all(|scope|*scope==Scope::default())
}
