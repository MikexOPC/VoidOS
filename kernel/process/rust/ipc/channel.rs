use core::mem::{align_of, size_of};

pub const MAX_MESSAGE: usize = 128;
const CAPACITY: usize = 4;
const INVALID: u64 = (-5i64) as u64;
const WOULD_BLOCK: u64 = (-6i64) as u64;
const PEER_CLOSED: u64 = (-7i64) as u64;
const BUFFER_SMALL: u64 = (-8i64) as u64;
const BAD_LENGTH: u64 = (-3i64) as u64;
const OPEN: u64 = 1;
const CLOSED: u64 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
struct Message {
    len: u64,
    bytes: [u8; MAX_MESSAGE],
}

const EMPTY: Message = Message { len: 0, bytes: [0; MAX_MESSAGE] };

#[repr(C)]
#[derive(Clone, Copy)]
struct Queue {
    messages: [Message; CAPACITY],
    head: u64,
    len: u64,
}

const EMPTY_QUEUE: Queue = Queue { messages: [EMPTY; CAPACITY], head: 0, len: 0 };

#[repr(C)]
pub struct Channel {
    generation: u64,
    phase: u64,
    queues: [Queue; 2],
}

const _: () = assert!(size_of::<Channel>() == 142 * 8 && align_of::<Channel>() == 8);

impl Channel {
    // ====== open ======

    fn open(&mut self) -> u64 {
        if self.phase != 0 || self.generation == u64::MAX >> 1 {
            return 0;
        }
        self.generation += 1;
        self.phase = OPEN;
        self.generation << 1
    }

    // ====== close ======

    fn close(&mut self) {
        self.queues = [EMPTY_QUEUE; 2];
        self.phase = CLOSED;
    }

    // ====== side ======

    fn side(&self, owner: u64, handle: u64) -> Result<usize, u64> {
        if self.phase == 0 || owner != handle || handle >> 1 != self.generation {
            return Err(INVALID);
        }
        if self.phase == CLOSED {
            return Err(PEER_CLOSED);
        }
        Ok((owner & 1) as usize)
    }

    // ====== send_slot ======

    fn send_slot(&self, owner: u64, handle: u64, len: u64) -> Result<usize, u64> {
        let side = self.side(owner, handle)?;
        if !(1..=MAX_MESSAGE as u64).contains(&len) {
            return Err(BAD_LENGTH);
        }
        if self.queues.get(side).ok_or(INVALID)?.len >= CAPACITY as u64 {
            return Err(WOULD_BLOCK);
        }
        Ok(side)
    }

    // ====== send ======

    fn send(&mut self, owner: u64, handle: u64, bytes: &[u8; MAX_MESSAGE], len: u64) -> Result<u64, u64> {
        let side = self.send_slot(owner, handle, len)?;
        let queue = self.queues.get_mut(side).ok_or(INVALID)?;
        let tail = (queue.head + queue.len) as usize % CAPACITY;
        let message = queue.messages.get_mut(tail).ok_or(INVALID)?;
        let target = message.bytes.get_mut(..len as usize).ok_or(BAD_LENGTH)?;
        let source = bytes.get(..len as usize).ok_or(BAD_LENGTH)?;
        for (target, source) in target.iter_mut().zip(source) {
            *target = *source;
        }
        message.len = len;
        queue.len += 1;
        Ok(len)
    }

    // ====== receive_slot ======

    fn receive_slot(&self, owner: u64, handle: u64) -> Result<usize, u64> {
        let side = self.side(owner, handle)? ^ 1;
        if self.queues.get(side).ok_or(INVALID)?.len == 0 {
            return Err(WOULD_BLOCK);
        }
        Ok(side)
    }

    // ====== peek ======

    fn peek(&self, owner: u64, handle: u64, buffer: &mut [u8; MAX_MESSAGE], capacity: u64) -> Result<u64, u64> {
        let queue = self.queues.get(self.receive_slot(owner, handle)?).ok_or(INVALID)?;
        let message = queue.messages.get(queue.head as usize).ok_or(INVALID)?;
        if message.len > capacity {
            return Err(BUFFER_SMALL);
        }
        let target = buffer.get_mut(..message.len as usize).ok_or(BAD_LENGTH)?;
        let source = message.bytes.get(..message.len as usize).ok_or(BAD_LENGTH)?;
        for (target, source) in target.iter_mut().zip(source) {
            *target = *source;
        }
        Ok(message.len)
    }

    // ====== consume ======

    fn consume(&mut self, owner: u64, handle: u64) -> Result<u64, u64> {
        let side = self.receive_slot(owner, handle)?;
        let queue = self.queues.get_mut(side).ok_or(INVALID)?;
        *queue.messages.get_mut(queue.head as usize).ok_or(INVALID)? = EMPTY;
        queue.head = (queue.head + 1) % CAPACITY as u64;
        queue.len -= 1;
        Ok(0)
    }
}

// ====== x86_ipc_open ======

/// # Safety
/// Channel storage must be initialized and exclusively borrowed by this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_open(channel: &mut Channel) -> u64 {
    channel.open()
}

// ====== x86_ipc_close ======

/// # Safety
/// Channel storage must be initialized and exclusively borrowed by this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_close(channel: &mut Channel) {
    channel.close();
}

// ====== x86_ipc_finish ======

/// # Safety
/// Channel storage must be initialized and exclusively borrowed by this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_finish(channel: &mut Channel) {
    channel.close();
    channel.phase = 0;
}

// ====== x86_ipc_send_check ======

/// # Safety
/// Channel storage must be initialized and readable for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_send_check(channel: &Channel, owner: u64, handle: u64, len: u64) -> u64 {
    channel.send_slot(owner, handle, len).map_or_else(|error| error, |_| 0)
}

// ====== x86_ipc_send ======

/// # Safety
/// Channel and kernel buffer must be initialized, valid and disjoint.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_send(
    channel: &mut Channel, owner: u64, handle: u64, bytes: &[u8; MAX_MESSAGE], len: u64,
) -> u64 {
    channel.send(owner, handle, bytes, len).unwrap_or_else(|error| error)
}

// ====== x86_ipc_peek ======

/// # Safety
/// Channel and kernel buffer must be initialized, valid and disjoint.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_peek(
    channel: &Channel, owner: u64, handle: u64, bytes: &mut [u8; MAX_MESSAGE], capacity: u64,
) -> u64 {
    channel.peek(owner, handle, bytes, capacity).unwrap_or_else(|error| error)
}

// ====== x86_ipc_consume ======

/// # Safety
/// The channel must be exclusively borrowed and the dispatcher must have completed the copy.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_ipc_consume(channel: &mut Channel, owner: u64, handle: u64) -> u64 {
    channel.consume(owner, handle).unwrap_or_else(|error| error)
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== channel ======

    fn channel() -> Channel {
        Channel { generation: 0, phase: 0, queues: [EMPTY_QUEUE; 2] }
    }

    // ====== fifo_keeps_head_until_consume ======

    #[test]
    fn fifo_keeps_head_until_consume() {
        let mut channel = channel();
        let left = channel.open();
        let right = left | 1;
        let mut output = [0; MAX_MESSAGE];
        assert_eq!(channel.peek(right, right, &mut output, 1), Err(WOULD_BLOCK));
        for value in 1..=4 {
            assert_eq!(channel.send(left, left, &[value; MAX_MESSAGE], 1), Ok(1));
        }
        assert_eq!(channel.send(left, left, &[9; MAX_MESSAGE], 1), Err(WOULD_BLOCK));
        assert_eq!(channel.peek(right, right, &mut output, 0), Err(BUFFER_SMALL));
        assert_eq!(channel.peek(right, right, &mut output, 1), Ok(1));
        assert_eq!(output[0], 1);
        assert_eq!(channel.peek(right, right, &mut output, 1), Ok(1));
        assert_eq!(output[0], 1);
        assert_eq!(channel.consume(right, right), Ok(0));
        assert_eq!(channel.send(left, left, &[5; MAX_MESSAGE], 1), Ok(1));
        for value in 2..=5 {
            assert_eq!(channel.peek(right, right, &mut output, 1), Ok(1));
            assert_eq!(output[0], value);
            assert_eq!(channel.consume(right, right), Ok(0));
        }
        assert_eq!(channel.send(right, right, &[7; MAX_MESSAGE], 128), Ok(128));
        assert_eq!(channel.peek(left, left, &mut output, 128), Ok(128));
        assert_eq!(output, [7; MAX_MESSAGE]);
    }

    // ====== handles_lengths_and_close ======

    #[test]
    fn handles_lengths_and_close() {
        let mut channel = channel();
        let left = channel.open();
        let right = left | 1;
        let bytes = [1; MAX_MESSAGE];
        let mut output = [0; MAX_MESSAGE];
        assert_eq!(channel.send(left, right, &bytes, 1), Err(INVALID));
        assert_eq!(channel.send(left, left, &bytes, 0), Err(BAD_LENGTH));
        assert_eq!(channel.send(left, left, &bytes, 129), Err(BAD_LENGTH));
        assert_eq!(channel.send(left, left, &bytes, 1), Ok(1));
        assert_eq!(channel.send(right, right, &bytes, 1), Ok(1));
        channel.close();
        assert_eq!(channel.send(left, left, &bytes, 1), Err(PEER_CLOSED));
        assert_eq!(channel.peek(right, right, &mut output, 128), Err(PEER_CLOSED));
        assert!(channel.queues.iter().all(|queue| queue.len == 0 &&
            queue.messages.iter().all(|message| message.bytes == EMPTY.bytes)));
        unsafe { x86_ipc_finish(&mut channel) };
        let next = channel.open();
        assert_ne!(next, left);
        assert_eq!(channel.send(next, left, &bytes, 1), Err(INVALID));
        channel.phase = 0;
        channel.generation = u64::MAX >> 1;
        assert_eq!(channel.open(), 0);
    }
}
