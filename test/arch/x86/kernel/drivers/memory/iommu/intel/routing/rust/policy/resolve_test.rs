use super::*;

// ====== fixture ======

fn fixture()->(Info,Vec<Node>,Request) {
    let mut info=Info { bits:48,units:2,scopes:1,..Info::default() };
    info.unit[0]=Unit { base:0xfed90000,bytes:4096,count:1,..Unit::default() };
    info.unit[1]=Unit { base:0xfed91000,bytes:4096,flags:1,first:1,..Unit::default() };
    info.scope[0]=Scope { kind:1,depth:2,path:[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],..Scope::default() };
    let mut nodes=vec![Node { id:0x00011b36,class:0x06040001,header:1<<16,buses:0x010100,device:1,..Node::default() },
        Node { id:0x10501af4,class:0x03000001,bus:1,..Node::default() },
        Node { id:0x000129c0,class:0x06000001,..Node::default() }];
    assert_ne!(graph::build(&mut nodes),0);
    (info,nodes,Request { bus:1,id:0x10501af4,..Request::default() })
}

// ====== explicit_and_fallback ======

#[test]
fn explicit_and_fallback() {
    let (mut info,nodes,request)=fixture();
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,0,1));
    info.scope[0].depth=1; info.scope[0].path=[0;16];
    assert_eq!(resolve(&info,&nodes,&request,48),encode(1,3,32,1));
    info.scope[0].kind=2; info.scope[0].path[0]=1;
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,2,0,1));
    info.unit[1].segment=7; info.scope[0].kind=1; info.scope[0].path[0]=0;
    assert_eq!(resolve(&info,&nodes,&request,48),1);
}

// ====== reject_paths_and_ambiguity ======

#[test]
fn reject_paths_and_ambiguity() {
    for mutation in 0..12 {
        let (mut info,mut nodes,mut request)=fixture();
        match mutation {
            0=>info.scope[0].path[0]=31,
            1=>info.scope[0].depth=3,
            2=>info.scope[0].kind=2,
            3=>{info.scope[0].depth=1;info.scope[0].bus=1;info.scope[0].path=[0;16];},
            4=>info.other=1,
            5=>info.scope[0].kind=5,
            6=>request.id^=1,
            7=>nodes[1].parent=u32::MAX,
            8=>info.scope[0].path[15]=1,
            9=>info.unit[1].first=0,
            10=>info.unit[1].base=info.unit[0].base,
            _=>info.unit[1].flags=3,
        }
        assert_eq!(resolve(&info,&nodes,&request,48),0,"mutation {mutation}");
    }
    let (mut info,nodes,request)=fixture();
    info.units=3; info.scopes=2; info.unit[1].flags=0; info.unit[1].count=1;
    info.unit[2]=Unit { base:0xfed92000,bytes:4096,flags:1,first:2,..Unit::default() };
    info.scope[1]=info.scope[0]; info.scope[1].unit=1;
    assert_eq!(resolve(&info,&nodes,&request,48),0);
    info.scope[1].kind=2; info.scope[1].depth=1; info.scope[1].path=[0;16]; info.scope[1].path[0]=1;
    assert_eq!(resolve(&info,&nodes,&request,48),0);
}

// ====== non_pci_and_limits ======

#[test]
fn non_pci_and_limits() {
    let (mut info,nodes,request)=fixture();
    for kind in 3..=4 {
        info.scope[0].kind=kind; info.scope[0].depth=1; info.scope[0].path=[0;16];
        assert_eq!(resolve(&info,&nodes,&request,48),encode(1,3,32,1));
    }
    for count in 0..=9 {
        let (mut info,nodes,request)=fixture(); info.units=count;
        if count!=2 { assert_eq!(resolve(&info,&nodes,&request,48),0); }
    }
    let (info,nodes,request)=fixture();
    for bits in 0..=64 { assert_eq!(resolve(&info,&nodes,&request,bits)!=0,(32..=52).contains(&bits)); }
}

// ====== maximum_units_scopes_and_order ======

#[test]
fn maximum_units_scopes_and_order() {
    let (_,mut nodes,request)=fixture();
    let mut info=Info { bits:48,units:8,..Info::default() };
    for (index,unit) in info.unit.iter_mut().enumerate() {
        *unit=Unit { base:0xfed90000+(index as u64)*4096,bytes:4096,segment:index as u16+1,flags:1,..Unit::default() };
    }
    info.unit[7].segment=0;
    assert_eq!(resolve(&info,&nodes,&request,48),encode(7,3,32,1));
    nodes.rotate_left(1); assert_ne!(graph::build(&mut nodes),0);
    assert_eq!(resolve(&info,&nodes,&request,48),encode(7,3,32,0));
    let mut nodes=Vec::new();
    for device in 0..32 { nodes.push(Node { id:0x10501af4,class:0x03000001,device,..Node::default() }); }
    assert_eq!(graph::build(&mut nodes),1);
    info=Info { bits:48,units:2,scopes:31,..Info::default() };
    info.unit[0]=Unit { base:0xfed90000,bytes:4096,count:31,..Unit::default() };
    info.unit[1]=Unit { base:0xfed91000,bytes:4096,flags:1,first:31,..Unit::default() };
    for (device,scope) in info.scope.iter_mut().enumerate().take(31) {
        *scope=Scope { kind:1,depth:1,..Scope::default() }; scope.path[0]=device as u8;
    }
    let request=Request { device:31,id:0x10501af4,..Request::default() };
    assert_eq!(resolve(&info,&nodes,&request,48),encode(1,3,32,31));
    info.scopes=32; info.unit[0].count=32; info.unit[1].first=32;
    info.scope[31]=Scope { kind:1,depth:1,..Scope::default() }; info.scope[31].path[0]=31;
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,31,31));
    info.scopes=33; assert_eq!(resolve(&info,&nodes,&request,48),0);
}

// ====== eight_pair_path ======

#[test]
fn eight_pair_path() {
    let mut nodes=Vec::new();
    for bus in 0..7 {
        nodes.push(Node { id:0x00011b36,class:0x06040001,header:1<<16,device:1,bus,
            buses:u32::from(bus)|(u32::from(bus+1)<<8)|(7<<16),..Node::default() });
    }
    nodes.push(Node { id:0x10501af4,class:0x03000001,bus:7,..Node::default() });
    assert_eq!(graph::build(&mut nodes),8);
    let mut info=Info { bits:48,units:1,scopes:1,..Info::default() };
    info.unit[0]=Unit { base:0xfed90000,bytes:4096,count:1,..Unit::default() };
    info.scope[0]=Scope { kind:1,depth:8,..Scope::default() };
    for pair in 0..7 { info.scope[0].path[pair*2]=1; }
    let request=Request { bus:7,id:0x10501af4,..Request::default() };
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,0,7));
    info.scope[0].depth=9; assert_eq!(resolve(&info,&nodes,&request,48),0);
}

// ====== nonzero_segment_and_root ======

#[test]
fn nonzero_segment_and_root() {
    let (mut info,mut nodes,mut request)=fixture();
    for node in &mut nodes {
        node.segment=7; node.bus+=20;
        if node.header==1<<16 { node.buses=20|(21<<8)|(21<<16); }
    }
    let mut other=nodes.clone(); for node in &mut other { node.segment=0; }
    nodes.extend(other); assert_ne!(graph::build(&mut nodes),0);
    info.unit[0].segment=7; info.unit[1].segment=7; info.scope[0].bus=20;
    request.segment=7; request.bus=21;
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,0,1));
    info.unit[0].segment=0;
    assert_eq!(resolve(&info,&nodes,&request,48),encode(1,3,32,1));
    info.unit[1].segment=0;
    assert_eq!(resolve(&info,&nodes,&request,48),1);
}

// ====== endpoint_classes ======

#[test]
fn endpoint_classes() {
    let (info,mut nodes,request)=fixture();
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,0,1));
    if let Some(node)=nodes.get_mut(1) { node.class=0xff000001; }
    assert_eq!(resolve(&info,&nodes,&request,48),encode(0,1,0,1));
    if let Some(node)=nodes.get_mut(1) { node.class=0x06800001; }
    assert_eq!(resolve(&info,&nodes,&request,48),0);
}
