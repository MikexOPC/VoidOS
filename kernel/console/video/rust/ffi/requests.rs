#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../requests/queue.rs"]
mod queue;
#[cfg(console_ap_diagnostic)]
mod channel;
#[cfg(console_ap_notify)]
mod notify;
use queue::{Error, Operation, Queue, State, TEXT_LIMIT};
use core::mem::{align_of, size_of};

#[repr(C)]
struct Packet {
    operation: u32,
    length: u32,
    text: [u8; TEXT_LIMIT],
}

const _: () = {
    assert!(size_of::<Queue>() <= 320 * 8);
    assert!(align_of::<Queue>() <= 8);
    assert!(size_of::<Packet>() == 264);
    assert!(align_of::<Packet>() == 4);
    assert!(core::mem::offset_of!(Packet, text) == 8);
};

// ====== status ======

fn status(result: Result<(), Error>) -> u32 {
    match result {
        Ok(()) => 0,
        Err(Error::Invalid) => 1,
        Err(Error::Full) => 2,
        Err(Error::Empty) => 3,
        Err(Error::State) => 4,
        Err(Error::InFlight) => 5,
        Err(Error::Pending) => 6,
    }
}

// ====== execute ======

fn execute(owner: &mut Queue, command: u32, packet: &mut Packet) -> Result<(), Error> {
    match command {
        1 => {
            let operation = match packet.operation {
                1 => Operation::Write, 2 => Operation::Edit, 3 => Operation::Submit,
                _ => return Err(Error::Invalid),
            };
            let length = packet.length as usize;
            if length > TEXT_LIMIT { return Err(Error::Invalid); }
            owner.push(operation, &packet.text[..length])
        }
        2 => {
            let request = owner.take()?;
            let mut reply = Packet { operation: match request.operation() {
                Operation::Write => 1, Operation::Edit => 2, Operation::Submit => 3,
            }, length: request.text().len() as u32, text: [0; TEXT_LIMIT] };
            reply.text[..request.text().len()].copy_from_slice(request.text());
            *packet = reply;
            Ok(())
        }
        3 if packet.operation <= 1 => owner.complete(packet.operation == 1),
        4 => owner.drain(),
        5 => owner.close(),
        6 => {
            packet.operation = match owner.state() {
                State::Open => 1, State::Draining => 2, State::Closed => 3, State::Failed => 4,
            };
            packet.length = owner.pending() as u32;
            Ok(())
        }
        _ => Err(Error::Invalid),
    }
}

// ====== kernel_console_requests_raw ======

#[unsafe(no_mangle)]
unsafe extern "C" fn kernel_console_requests_raw(storage: *mut u8, capacity: usize,
    command: u32, packet: *mut Packet) -> u32 {
    if storage.is_null() || !(storage as usize).is_multiple_of(8) ||
        capacity < size_of::<Queue>() || packet.is_null() ||
        !(packet as usize).is_multiple_of(align_of::<Packet>()) {
        return 1;
    }
    if command == 0 {
        unsafe { storage.cast::<Queue>().write(Queue::new()); }
        return 0;
    }
    let owner = unsafe { &mut *storage.cast::<Queue>() };
    let packet = unsafe { &mut *packet };
    status(execute(owner, command, packet))
}

unsafe extern "C" { fn kernel_console_requests_fail_stop() -> !; }

// ====== requests_panic ======

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { kernel_console_requests_fail_stop() }
}
