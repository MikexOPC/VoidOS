use super::*;

// ====== channel_ownership_and_reuse ======

#[test]
fn channel_ownership_and_reuse() {
    let mut channel = Channel::new();
    assert!(!channel.enabled()); assert_eq!(channel.push(3), 0);
    assert_eq!(channel.take(0, 1), DENIED); assert!(!channel.close());
    assert!(!channel.open(8)); assert!(channel.open(0)); assert!(!channel.open(1));
    assert_eq!(channel.push(256), 0); assert_eq!(channel.push(42), 1);
    assert!(!channel.close()); assert_eq!(channel.take(1, 1), DENIED);
    assert_eq!(channel.take(0, 0), DENIED); assert_eq!(channel.take(0, 3), DATA | 42);
    assert_eq!(channel.take(0, 2), DENIED); assert_eq!(channel.take(0, 3), EMPTY);
    assert!(channel.close()); assert_eq!(channel, Channel::new());
    assert!(channel.open(1)); assert_eq!(channel.take(0, 3), DENIED);
    assert_eq!(channel.take(1, 4), EMPTY); assert!(channel.close());
}

// ====== channel_cannot_close_with_unobserved_loss ======

#[test]
fn channel_cannot_close_with_unobserved_loss() {
    let mut channel = Channel::new(); assert!(channel.open(0));
    for byte in 0..256 { assert_eq!(channel.push(byte), 1); }
    assert_eq!(channel.push(9), 2);
    for byte in 0u32..256 { assert_eq!(channel.take(0, 1), DATA | u64::from(byte)); }
    assert!(!channel.close()); assert_eq!(channel.take(0, 1), LOSS | (1 << 32));
    assert!(channel.close());
}
