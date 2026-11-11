#![forbid(unsafe_code)]

use crate::bridge_protocol::{StatusReply, STATUS, VERSION};

// ====== reply ======

pub fn reply(request: &[u8]) -> StatusReply {
    match request {
        [VERSION, STATUS] => StatusReply::Ready,
        [VERSION, _] => StatusReply::UnsupportedOperation,
        [_, _] => StatusReply::UnsupportedVersion,
        _ => StatusReply::Malformed,
    }
}
