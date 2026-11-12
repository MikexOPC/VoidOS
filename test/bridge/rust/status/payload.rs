use crate::{exit, ipc};

const RECEIVE: u64 = 4;
const SEND: u64 = 3;
const WAIT_READABLE: u64 = 5;
const PEER_CLOSED: i64 = -7;

#[cfg(program_bridge_client)]
const CASES: [(&[u8], [u8; 2]); 5] = [
    (&[1, 1], [1, 0]),
    (&[1], [1, 1]),
    (&[0; 128], [1, 1]),
    (&[2, 1], [1, 2]),
    (&[1, 2], [1, 3]),
];

// ====== program_main ======

#[cfg(program_bridge_client)]
#[unsafe(no_mangle)]
extern "C" fn program_main(handle: u64) -> ! {
    let mut response = [0u8; 2];
    for (request, expected) in CASES {
        let length = request.len() as u64;
        if ipc(SEND, handle, request.as_ptr() as u64, length) != length as i64 {
            exit(1);
        }
        match ipc(WAIT_READABLE, handle, 0, 0) {
            0 => {}
            PEER_CLOSED => exit(77),
            _ => exit(1),
        }
        if ipc(RECEIVE, handle, response.as_mut_ptr() as u64, 2) != 2 || response != expected {
            exit(2);
        }
    }
    exit(0);
}

// ====== program_main ======

#[cfg(program_bridge_server)]
#[unsafe(no_mangle)]
extern "C" fn program_main(handle: u64) -> ! {
    let mut request = [0u8; 128];
    let capacity = request.len() as u64;
    let pointer = request.as_mut_ptr() as u64;
    for _ in 0..5 {
        if ipc(WAIT_READABLE, handle, 0, 0) != 0 {
            exit(3);
        }
        let length = ipc(RECEIVE, handle, pointer, capacity);
        if length <= 0 {
            exit(3);
        }
        let Some(frame) = request.get(..length as usize) else {
            exit(3);
        };
        let response = crate::bridge_service::reply(frame).encode();
        if ipc(SEND, handle, response.as_ptr() as u64, response.len() as u64) != response.len() as i64 {
            exit(4);
        }
    }
    exit(if ipc(WAIT_READABLE, handle, 0, 0) == PEER_CLOSED { 0 } else { 5 });
}
