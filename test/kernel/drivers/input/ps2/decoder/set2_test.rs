use super::*;

// ====== decode_sequence ======

fn events(bytes: &[u8]) -> Vec<Key> {
    let mut decoder = Decoder::new();
    bytes.iter().filter_map(|byte| match decoder.feed(*byte) {
        Output::Key(key) => Some(key),
        Output::Pending => None,
        Output::Invalid => panic!("invalid fixture byte {byte:#x}"),
    }).collect()
}

// ====== ordinary_make_break_and_repeat ======

#[test]
fn ordinary_make_break_and_repeat() {
    assert_eq!(events(&[0x1c, 0x1c, 0xf0, 0x1c]), vec![
        Key { code: 0x1c, action: Action::Press },
        Key { code: 0x1c, action: Action::Press },
        Key { code: 0x1c, action: Action::Release },
    ]);
}

// ====== extended_keys_and_boundaries ======

#[test]
fn extended_keys_and_boundaries() {
    assert_eq!(events(&[0xe0, 0x75, 0xe0, 0xf0, 0x75]), vec![
        Key { code: 0x175, action: Action::Press },
        Key { code: 0x175, action: Action::Release },
    ]);
    assert_eq!(events(&[0x83, 0x84, 1]).len(), 3);
}

// ====== pause_is_a_single_pulse ======

#[test]
fn pause_is_a_single_pulse() {
    assert_eq!(events(&[0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77]),
        vec![Key { code: PAUSE, action: Action::Pulse }]);
    // Ctrl+Pause is represented by the controller as an extended make/break.
    assert_eq!(events(&[0xe0, 0x7e, 0xe0, 0xf0, 0x7e]).len(), 2);
}

// ====== print_screen_does_not_leak_fake_shift ======

#[test]
fn print_screen_does_not_leak_fake_shift() {
    let expected = vec![Key { code: PRINT_SCREEN, action: Action::Press },
        Key { code: PRINT_SCREEN, action: Action::Release }];
    assert_eq!(events(&[0xe0, 0x12, 0xe0, 0x7c, 0xe0, 0xf0, 0x7c, 0xe0, 0xf0, 0x12]), expected);
    assert_eq!(events(&[0xe0, 0x7c, 0xe0, 0xf0, 0x7c]), expected);
    // Alt+Print produces the distinct physical SysRq code 0x84.
    assert_eq!(events(&[0xf0, 0x11, 0x11, 0x84])[2].code, 0x84);
}

// ====== replies_and_error_bytes_never_become_keys ======

#[test]
fn replies_and_error_bytes_never_become_keys() {
    for byte in [0, 0xaa, 0xab, 0xee, 0xfa, 0xfc, 0xfd, 0xfe, 0xff, 0x85] {
        let mut decoder = Decoder::new();
        assert_eq!(decoder.feed(0xf0), Output::Pending);
        assert_eq!(decoder.feed(byte), Output::Invalid);
        assert_eq!(decoder.state, State::Idle);
        assert_eq!(decoder.feed(0x1c), Output::Key(Key { code: 0x1c, action: Action::Press }));
    }
}

// ====== repeated_prefixes_are_rejected ======

#[test]
fn repeated_prefixes_are_rejected() {
    for bytes in [[0xf0, 0xf0], [0xe0, 0xe0], [0xf0, 0xe0], [0xe0, 0xe1]] {
        let mut decoder = Decoder::new();
        assert_eq!(decoder.feed(bytes[0]), Output::Pending);
        assert_eq!(decoder.feed(bytes[1]), Output::Invalid);
        assert_eq!(decoder.state, State::Idle);
    }
}

// ====== pause_mutations_do_not_emit_partial_events ======

#[test]
fn pause_mutations_do_not_emit_partial_events() {
    for position in 0..7 {
        for replacement in 0..=255 {
            if replacement == PAUSE_TAIL[position] { continue; }
            let mut decoder = Decoder::new();
            assert_eq!(decoder.feed(0xe1), Output::Pending);
            for byte in &PAUSE_TAIL[..position] { assert_eq!(decoder.feed(*byte), Output::Pending); }
            assert_eq!(decoder.feed(replacement), Output::Invalid);
            assert_eq!(decoder.state, State::Idle);
        }
    }
}

// ====== loss_discards_every_partial_state ======

#[test]
fn loss_discards_every_partial_state() {
    for state in [State::Idle, State::Break, State::Extended, State::ExtendedBreak,
        State::Pause(0), State::Pause(1), State::Pause(2), State::Pause(3),
        State::Pause(4), State::Pause(5), State::Pause(6)] {
        let mut decoder = Decoder { state };
        decoder.reset();
        assert_eq!(decoder.feed(0x1c), Output::Key(Key { code: 0x1c, action: Action::Press }));
    }
}

// ====== exhaustive_byte_pairs_keep_state_and_output_bounded ======

#[test]
fn exhaustive_byte_pairs_keep_state_and_output_bounded() {
    for first in 0..=255 {
        for second in 0..=255 {
            let mut decoder = Decoder::new();
            for byte in [first, second] { bounded(&mut decoder, byte); }
        }
    }
}

// ====== deterministic_stream_stress ======

#[test]
fn deterministic_stream_stress() {
    let mut decoder = Decoder::new();
    let mut random = 19u32;
    for index in 0..1_000_000 {
        random = random.wrapping_mul(1664525).wrapping_add(1013904223);
        bounded(&mut decoder, (random >> 24) as u8);
        if index % 257 == 0 { decoder.reset(); }
    }
}

// ====== serialized_transition_validates_full_width_inputs ======

#[test]
fn serialized_transition_validates_full_width_inputs() {
    for state in [4, 15, 23, 255, 65536, u32::MAX] {
        assert_eq!(step(state, 0x1c), 2 << 24);
    }
    for byte in [256, 65536, u32::MAX] { assert_eq!(step(0, byte), 2 << 24); }
    assert_eq!(step(0, 0xe0), (2 << 32) | (1 << 24));
    assert_eq!(step(2, 0xf0), (3 << 32) | (1 << 24));
    assert_eq!(step(3, 0x75), 0x175 | (2 << 16));
    let mut state = 0;
    for byte in [0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77] {
        let result = step(state, byte);
        state = (result >> 32) as u32;
    }
    assert_eq!(state, 0);
    assert_eq!(step(22, 0x77), u64::from(PAUSE) | (3 << 16));
}

// ====== bounded ======

fn bounded(decoder: &mut Decoder, byte: u8) {
    if let Output::Key(key) = decoder.feed(byte) {
        assert!(key.code <= 0x184 || key.code == PAUSE);
        assert_ne!(key.code, 0);
        assert_ne!(key.code, 0x112);
        if key.action == Action::Pulse { assert_eq!(key.code, PAUSE); }
    }
    if let State::Pause(index) = decoder.state { assert!(index < 7); }
}
