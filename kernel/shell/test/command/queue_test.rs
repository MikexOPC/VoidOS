use super::*;

// ====== full_fifo_close_and_wrap ======

#[test]
fn full_fifo_close_and_wrap() {
    let mut state = 0;
    for _ in 0..10000 {
        for index in 0..4 {
            let result = apply(state,1); assert_eq!(result as u32,index << 8);
            state = (result >> 32) as u32;
        }
        assert_eq!(apply(state,1),(u64::from(state) << 32) | 2);
        for index in 0..4 {
            let result = apply(state,2); assert_eq!(result as u32,index << 8);
            state = (result >> 32) as u32;
        }
        assert_eq!(apply(state,2),(u64::from(state) << 32) | 1);
    }
    state = (apply(state,1) >> 32) as u32;
    state = (apply(state,3) >> 32) as u32;
    assert_eq!(apply(state,1),(u64::from(state) << 32) | 3);
    let popped = apply(state,2); assert_eq!(popped as u32,0);
    state = (popped >> 32) as u32;
    assert_eq!(apply(state,2),(u64::from(state) << 32) | 3);
}

// ====== every_state_and_operation ======

#[test]
fn every_state_and_operation() {
    for state in 0..=256 {
        for op in 0..=5 {
            let result = apply(state,op);
            let valid = state < 256 && (state >> 4) & 7 <= 4 &&
                (state >> 2) & 3 == ((state & 3) + ((state >> 4) & 7)) % 4 && (1..=4).contains(&op);
            if !valid { assert_eq!(result,u64::from(u32::MAX)); }
            else {
                let next = (result >> 32) as u32;
                assert!(next <= 255);
                assert_eq!((next >> 2) & 3,((next & 3) + ((next >> 4) & 7)) % 4);
            }
        }
    }
    assert_eq!(apply(u32::MAX,1),u64::from(u32::MAX));
}
