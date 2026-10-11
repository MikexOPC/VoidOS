use super::*;

// ====== bind_bounds_identity_and_stale_tokens ======

#[test]
fn bind_bounds_identity_and_stale_tokens() {
    let mut bindings = Bindings::new();
    for event in [8, u32::MAX] { assert_eq!(bindings.attach(event, 1, false), 0); }
    assert_eq!(bindings.attach(0, 0, false), 0);
    assert_eq!(bindings.attach(0, 1, true), 0);
    assert_eq!(bindings.attach(0, 1, false), 1);
    assert_eq!(bindings.attach(0, 2, false), 0);
    assert_eq!(bindings.attach(1, 1, false), 0);
    assert!(!bindings.matches(0, 2, 1));
    assert!(!bindings.matches(0, 1, 0));
    assert!(!bindings.detach(0, 1, 1, true));
    assert!(bindings.matches(0, 1, 1));
    assert!(bindings.detach(0, 1, 1, false));
    assert!(!bindings.detach(0, 1, 1, false));
    assert_eq!(bindings.attach(0, 1, false), 2);
    assert!(!bindings.matches(0, 1, 1));
    assert!(bindings.matches(0, 1, 2));
}

// ====== repeated_bindings_conserve_generations ======

#[test]
fn repeated_bindings_conserve_generations() {
    let mut bindings = Bindings::new();
    for generation in 1..=10000 {
        for event in 0..8 {
            let source = u64::from(event) + 1;
            assert_eq!(bindings.attach(event, source, false), generation);
            assert!(!bindings.detach(event, source, generation - 1, false));
            assert!(bindings.detach(event, source, generation, false));
        }
    }
}

// ====== saturation_and_foreign_detach_do_not_mutate ======

#[test]
fn saturation_and_foreign_detach_do_not_mutate() {
    let mut bindings = Bindings::new();
    bindings.entries[0].generation = u64::MAX;
    assert_eq!(bindings.attach(0, 1, false), 0);
    assert_eq!(bindings.entries[0], Entry { source: 0, generation: u64::MAX });
    assert_eq!(bindings.attach(1, 1, false), 1);
    for event in [0, 8, u32::MAX] { assert!(!bindings.detach(event, 1, 1, false)); }
    assert!(bindings.matches(1, 1, 1));
}
