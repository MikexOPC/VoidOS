use super::*;

// ====== transition ======

fn feed(state: &mut u32, code: u32, action: u32) -> u32 {
    let result = step(*state, code | action << 16);
    *state = (result >> 32) as u32;
    result as u32
}

// ====== shift_sides_release_and_repeat ======

#[test]
fn shift_sides_release_and_repeat() {
    let mut state = 0;
    assert_eq!(feed(&mut state,0x12,1),0);
    assert_eq!(feed(&mut state,0x59,1),0);
    assert_eq!(feed(&mut state,0x12,2),0);
    assert_eq!(feed(&mut state,0x1c,1),u32::from('A'));
    assert_eq!(feed(&mut state,0x1c,1),u32::from('A'));
    assert_eq!(feed(&mut state,0x1c,2),0);
    assert_eq!(feed(&mut state,0x59,2),0);
    assert_eq!(feed(&mut state,0x1c,1),u32::from('a'));
    assert_eq!(state,0);
}

// ====== caps_repeat_xor_shift_and_accent_policy ======

#[test]
fn caps_repeat_xor_shift_and_accent_policy() {
    let mut state = 0;
    for _ in 0..100 { assert_eq!(feed(&mut state,0x58,1),0); }
    assert_eq!(feed(&mut state,0x1c,1),u32::from('A'));
    assert_eq!(feed(&mut state,0x54,1),0xe8); // Windows Italian Caps affects A-Z only.
    feed(&mut state,0x12,1);
    assert_eq!(feed(&mut state,0x1c,1),u32::from('a'));
    assert_eq!(feed(&mut state,0x54,1),0xe9);
    feed(&mut state,0x12,2); feed(&mut state,0x58,2); feed(&mut state,0x58,1);
    assert_eq!(feed(&mut state,0x1c,1),u32::from('a'));
    feed(&mut state,0x58,2); assert_eq!(state,0);
}

// ====== italian_symbols_and_altgr ======

#[test]
fn italian_symbols_and_altgr() {
    let mut state = 0;
    for (code, plain, shifted) in [(0x55,0xec,0x5e),(0x54,0xe8,0xe9),
        (0x4c,0xf2,0xe7),(0x52,0xe0,0xb0),(0x5d,0xf9,0xa7),(0x26,0x33,0xa3),
        (0x61,0x3c,0x3e)] {
        assert_eq!(feed(&mut state,code,1),plain);
        feed(&mut state,0x12,1); assert_eq!(feed(&mut state,code,1),shifted);
        feed(&mut state,0x12,2);
    }
    feed(&mut state,0x111,1);
    for (code, scalar) in [(0x24,0x20ac),(0x2e,0x20ac),(0x54,0x5b),
        (0x5b,0x5d),(0x4c,0x40),(0x52,0x23)] {
        assert_eq!(feed(&mut state,code,1),scalar);
    }
    assert_eq!(feed(&mut state,0x1c,1),0);
    feed(&mut state,0x59,1);
    assert_eq!(feed(&mut state,0x54,1),0x7b);
    assert_eq!(feed(&mut state,0x5b,1),0x7d);
    assert_eq!(feed(&mut state,0x24,1),0);
    feed(&mut state,0x59,2); feed(&mut state,0x111,2); assert_eq!(state,0);
}

// ====== shortcuts_and_nontext_keys ======

#[test]
fn shortcuts_and_nontext_keys() {
    for modifier in [0x14,0x114,0x11] {
        let mut state = 0;
        feed(&mut state,modifier,1);
        assert_eq!(feed(&mut state,0x1c,1),0);
        feed(&mut state,modifier,2); assert_eq!(feed(&mut state,0x1c,1),0x61);
    }
    let mut state = 0;
    for (code, value) in [(0x66,BACKSPACE),(0x0d,TAB),(0x5a,ENTER)] {
        assert_eq!(feed(&mut state,code,1),value);
        assert_eq!(feed(&mut state,code,2),0);
    }
    assert_eq!(feed(&mut state,0x17c,1),0);
    assert_eq!(feed(&mut state,0x200,3),0);
    assert_eq!(feed(&mut state,0x175,1),0);
    assert_eq!(feed(&mut state,0x112,1),0); assert_eq!(state,0);
}

// ====== rejected_inputs_reset_and_exhaustive_bounds ======

#[test]
fn rejected_inputs_reset_and_exhaustive_bounds() {
    for event in [0,0x10000,0x10085,0x10200,0x3001c,0x4001c,u32::MAX] {
        assert_eq!(step(LS | CAPS,event),u64::from(INVALID));
    }
    assert_eq!(step(256,0x1001c),u64::from(INVALID));
    for state in 0..256 {
        for code in 0..=0x201 {
            for action in 0..=4 {
                let result = step(state,code | action << 16);
                assert!(result >> 32 <= 255);
                let scalar = result as u32;
                assert!(scalar == INVALID || scalar == 0 || matches!(scalar,BACKSPACE | TAB | ENTER) ||
                    char::from_u32(scalar).is_some());
            }
        }
    }
    // Reset after LOSS is explicit state0; no sticky modifier or lock survives.
    assert_eq!(step(0,0x1001c),0x61);
}
