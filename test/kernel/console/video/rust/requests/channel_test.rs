#[path = "../../../../../../kernel/console/video/rust/requests/channel.rs"]
mod state;
use state::Channel;

// ====== identity_and_reuse ======

#[test]
fn identity_and_reuse() {
    let channel = Channel::default();
    assert!(!channel.publish(1, 9));
    assert!(!channel.publish(0, 0));
    assert!(!channel.enter(1, 9, 0));
    assert_eq!(channel.mask(0), u64::MAX);
    assert!(channel.publish(0, 9));
    assert!(!channel.publish(0, 10));
    assert!(!channel.enter(0, 9, 0));
    assert!(!channel.enter(1, 9, 2));
    assert!(!channel.enter(1, 8, 0));
    assert_eq!(channel.mask(8), u64::MAX);
    assert!(!channel.close(0, 9));
    assert!(channel.complete(1, 9, 0));
    assert!(!channel.complete(1, 9, 0));
    assert!(!channel.enter(1, 9, 0));
    assert!(!channel.close(0, 9));
    assert!(channel.complete(1, 9, 1));
    assert!(!channel.close(1, 9));
    assert!(!channel.close(0, 8));
    assert!(channel.close(0, 9));
    assert_eq!(channel.mask(9), 7);
    assert!(!channel.close(0, 9));
    assert!(!channel.enter(1, 9, 1));
    assert!(!channel.publish(0, 11));
    let boundary = Channel::new();
    assert!(boundary.publish(0, u64::MAX));
    assert!(boundary.enter(1, u64::MAX, 0));
    assert_eq!(boundary.mask(u64::MAX), 0);
}

// ====== completion_publication ======

#[test]
fn completion_publication() {
    use std::sync::atomic::{AtomicU64, Ordering};
    for token in 1..=200 {
        let channel = Channel::new();
        let payload = [AtomicU64::new(0), AtomicU64::new(0)];
        assert!(channel.publish(0, token));
        std::thread::scope(|scope| {
            for (index, output) in payload.iter().enumerate() {
                let owner = &channel;
                scope.spawn(move || {
                    assert!(owner.enter(1, token, index as u32));
                    output.store(token + index as u64, Ordering::Relaxed);
                    assert!(owner.complete(1, token, index as u32));
                });
            }
        });
        assert_eq!(channel.mask(token), 3);
        assert_eq!(payload[0].load(Ordering::Relaxed), token);
        assert_eq!(payload[1].load(Ordering::Relaxed), token + 1);
        assert!(channel.close(0, token));
    }
}
