use super::*;

// ====== endpoint and port ======

fn endpoint(bus:u8,device:u8)->Node { Node { id:0x10501af4,class:0x03000001,bus,device,..Node::default() } }
fn port(bus:u8,device:u8,secondary:u8,last:u8)->Node {
    Node { id:0x00011b36,class:0x06040001,header:1<<16,buses:u32::from(bus)|(u32::from(secondary)<<8)|(u32::from(last)<<16),
        bus,device,..Node::default() }
}

// ====== nested ======

#[test]
fn nested() {
    let mut nodes=Vec::new();
    for bus in 0..8 { nodes.push(port(bus,1,bus+1,8)); }
    nodes.push(endpoint(8,0));
    assert_eq!(build(&mut nodes),9); assert_eq!(check(&nodes),9);
    assert_eq!(nodes.last().unwrap().depth,8);
    nodes.reverse(); assert_eq!(build(&mut nodes),9); assert_eq!(check(&nodes),9);
    for node in &mut nodes { if node.header==1<<16 { node.buses=(node.buses&0xff00ffff)|(9<<16); } }
    nodes.push(port(8,2,9,9)); nodes.push(endpoint(9,0)); assert_eq!(build(&mut nodes),0);
}

// ====== invalid_graphs ======

#[test]
fn invalid_graphs() {
    let mut duplicate=[endpoint(0,0),endpoint(0,0)]; assert_eq!(build(&mut duplicate),0);
    let mut cycle=[port(1,1,2,2),port(2,1,1,2)]; assert_eq!(build(&mut cycle),0);
    let mut siblings=[port(0,1,1,3),port(0,2,2,4)]; assert_eq!(build(&mut siblings),0);
    let mut orphan=[port(0,1,1,3),endpoint(2,0)]; assert_eq!(build(&mut orphan),0);
    let mut outside=[port(0,1,1,1),port(1,1,2,2)]; assert_eq!(build(&mut outside),0);
    let mut wrong=[port(0,1,1,1)]; wrong[0].class=0x03000000; assert_eq!(build(&mut wrong),0);
    let mut many=Vec::new(); for number in 0..65 {
        let mut node=port(0,number%32,number%32+1,number%32+1); node.segment=u16::from(number/32); many.push(node);
    }
    assert_eq!(build(&mut many),0);
}

// ====== segments_and_multifunction ======

#[test]
fn segments_and_multifunction() {
    let mut nodes=[port(0,1,1,1),endpoint(1,0),port(0,1,1,1),endpoint(1,0),endpoint(20,0)];
    nodes[2].segment=7; nodes[3].segment=7;
    assert_eq!(build(&mut nodes),3); assert_eq!(check(&nodes),3);
    nodes[3].parent=0; assert_eq!(check(&nodes),0);
    let mut multi=[endpoint(0,1),endpoint(0,1)]; multi[1].function=1;
    assert_eq!(build(&mut multi),0); multi[0].header=1<<23;
    assert_eq!(build(&mut multi),1); assert_eq!(check(&multi),1);
    multi[0].reserved[0]=1; assert_eq!(check(&multi),0);
}

// ====== boundaries ======

#[test]
fn boundaries() {
    assert_eq!(build(&mut []),1);
    let mut disabled=[port(0,1,0,0),endpoint(0,0)]; assert_eq!(build(&mut disabled),2);
    for value in 0..256 {
        let mut one=[endpoint(0,value as u8)];
        assert_eq!(build(&mut one)!=0,value<32);
    }
    let mut all=Vec::new();
    for bus in 0..16 { for device in 0..32 { all.push(endpoint(bus,device)); } }
    assert_eq!(build(&mut all),1); assert_eq!(check(&all),1);
    all.push(endpoint(16,0)); assert_eq!(build(&mut all),0);
}
