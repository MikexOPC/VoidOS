#[path = "../../../../bridge/api/rust/status/protocol.rs"]
mod bridge_protocol;
#[path = "../../../../bridge/service/rust/status/dispatch.rs"]
mod bridge_service;

use bridge_protocol::{StatusReply, STATUS, VERSION};
use bridge_service::reply;

// ====== reports_ready ======

#[test]
fn reports_ready() {
    assert_eq!(reply(&[VERSION, STATUS]).encode(), [1, 0]);
}

// ====== rejects_wrong_lengths ======

#[test]
fn rejects_wrong_lengths() {
    let bytes = [VERSION; 128];
    for length in 0..=bytes.len() {
        if length != 2 {
            assert_eq!(reply(&bytes[..length]).encode(), [1, 1]);
        }
    }
}

// ====== rejects_unsupported_versions ======

#[test]
fn rejects_unsupported_versions() {
    for version in 0..=u8::MAX {
        if version != VERSION {
            assert_eq!(reply(&[version, STATUS]), StatusReply::UnsupportedVersion);
        }
    }
    assert_eq!(reply(&[0, 0]).encode(), [1, 2]);
}

// ====== rejects_unsupported_operations ======

#[test]
fn rejects_unsupported_operations() {
    for operation in 0..=u8::MAX {
        if operation != STATUS {
            assert_eq!(reply(&[VERSION, operation]).encode(), [1, 3]);
        }
    }
}
