#![forbid(unsafe_code)]
// State: head1:0, tail3:2, count6:4, closed7. Single producer/consumer
// on BSP at IF=0; this integer machine is not a multi-core lock.
// ====== apply ======

pub fn apply(state: u32, operation: u32) -> u64 {
    let head = state & 3;
    let tail = (state >> 2) & 3;
    let count = (state >> 4) & 7;
    let closed = state & 128;
    if state > 255 || count > 4 || tail != (head + count) % 4 || !(1..=4).contains(&operation) {
        return u64::from(u32::MAX);
    }
    let (next, reply) = match operation {
        1 if closed != 0 => (state, 3),
        1 if count == 4 => (state, 2),
        1 => (head | (((tail + 1) % 4) << 2) | ((count + 1) << 4), tail << 8),
        2 if count == 0 => (state, if closed != 0 { 3 } else { 1 }),
        2 => (((head + 1) % 4) | (tail << 2) | ((count - 1) << 4) | closed, head << 8),
        3 => (state | 128, 0),
        _ => (state, if count == 0 { if closed != 0 { 3 } else { 1 } } else { 0 }),
    };
    (u64::from(next) << 32) | u64::from(reply)
}

#[cfg(test)]
#[path = "../../../../test/kernel/shell/command/queue_test.rs"]
mod tests;
