const HALT_RAX: &[u8] = b"rax=000000004b354641";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct KernelFault {
    pub error: u64,
    pub rip: u64,
    pub cr2: u64,
}

// ====== word_byte ======

fn word_byte(byte: u8) -> bool {
    byte == b'_' || byte.is_ascii_alphanumeric()
}

// ====== boundary ======

fn boundary(bytes: &[u8], index: usize) -> bool {
    let before = index > 0 && word_byte(bytes[index - 1]);
    let after = bytes.get(index).is_some_and(|byte| word_byte(*byte));
    before != after
}

// ====== line_end ======

fn line_end(bytes: &[u8], start: usize) -> usize {
    bytes[start..].iter().position(|byte| *byte == b'\r' || *byte == b'\n').map_or(bytes.len(), |at| start + at)
}

// ====== find_from ======

fn find_from(bytes: &[u8], start: usize, needle: &[u8], fold: bool) -> Option<usize> {
    if start > bytes.len() || bytes.len() - start < needle.len() {
        return None;
    }
    bytes[start..].windows(needle.len()).position(|window| {
        if fold { window.eq_ignore_ascii_case(needle) } else { window == needle }
    }).map(|at| start + at)
}

// ====== take_prefix ======

fn take_prefix(bytes: &[u8], at: usize, prefix: &[u8]) -> Option<usize> {
    let end = at + prefix.len();
    (end <= bytes.len() && bytes[at..end].eq_ignore_ascii_case(prefix)).then_some(end)
}

// ====== take_hex ======

fn take_hex(bytes: &[u8], at: usize) -> Option<(usize, u64)> {
    let digits = bytes[at..].iter().take_while(|byte| byte.is_ascii_hexdigit()).count();
    if digits == 0 {
        return None;
    }
    let text = std::str::from_utf8(&bytes[at..at + digits]).ok()?;
    Some((at + digits, u64::from_str_radix(text, 16).unwrap_or(u64::MAX)))
}

// ====== registers ======

pub fn registers(monitor: &[u8]) -> Option<(u64, u8)> {
    let mut start = 0;
    while let Some(at) = find_from(monitor, start, b"RIP=", false) {
        start = at + 1;
        let Some((hex_end, rip)) = take_hex(monitor, at + 4) else { continue };
        let end = line_end(monitor, hex_end);
        let mut cursor = end;
        while cursor > hex_end {
            cursor -= 1;
            if cursor + 5 <= end
                && monitor[cursor..].starts_with(b"CPL=")
                && matches!(monitor[cursor + 4], b'0'..=b'3')
                && boundary(monitor, cursor)
                && boundary(monitor, cursor + 5)
            {
                return Some((rip, monitor[cursor + 4] - b'0'));
            }
        }
    }
    None
}

// ====== halted ======

pub fn halted(monitor: &[u8]) -> bool {
    let mut start = 0;
    while let Some(at) = find_from(monitor, start, HALT_RAX, true) {
        start = at + 1;
        let mut cursor = at + HALT_RAX.len();
        for _ in 0..=4 {
            let end = line_end(monitor, cursor);
            let next = match (monitor.get(end), monitor.get(end + 1)) {
                (Some(b'\n'), _) => end + 1,
                (Some(b'\r'), Some(b'\n')) => end + 2,
                _ => break,
            };
            if line_is_halted(monitor, next) {
                return true;
            }
            cursor = next;
        }
    }
    false
}

// ====== line_is_halted ======

fn line_is_halted(monitor: &[u8], start: usize) -> bool {
    let Some(after) = take_prefix(monitor, start, b"rip=") else { return false };
    let end = line_end(monitor, after);
    let mut cursor = after;
    while let Some(at) = find_from(&monitor[..end], cursor, b"hlt=1", true) {
        if boundary(monitor, at) && boundary(monitor, at + 5) {
            return true;
        }
        cursor = at + 1;
    }
    false
}

// ====== user_faults ======

pub fn user_faults(trace: &[u8]) -> Vec<(String, String)> {
    let mut found = Vec::new();
    let mut start = 0;
    while let Some(at) = find_from(trace, start, b"v=", true) {
        start = at + 1;
        if !boundary(trace, at) {
            continue;
        }
        let vector = at + 2;
        let Some(vector_text) = trace.get(vector..vector + 2) else { continue };
        let known = [b"06", b"0d", b"0e"].iter().any(|item| vector_text.eq_ignore_ascii_case(*item));
        if !known {
            continue;
        }
        let Some(error_start) = take_prefix(trace, vector + 2, b" e=") else { continue };
        let digits = trace[error_start..].iter().take_while(|byte| byte.is_ascii_hexdigit()).count();
        if digits == 0 {
            continue;
        }
        let error_end = error_start + digits;
        let Some(end) = take_prefix(trace, error_end, b" i=0 cpl=3") else { continue };
        if !boundary(trace, end) {
            continue;
        }
        found.push((
            String::from_utf8_lossy(vector_text).into_owned(),
            String::from_utf8_lossy(&trace[error_start..error_end]).into_owned(),
        ));
        start = end;
    }
    found
}

// ====== kernel_faults ======

pub fn kernel_faults(trace: &[u8]) -> Vec<KernelFault> {
    let mut found = Vec::new();
    let mut start = 0;
    while let Some(at) = find_from(trace, start, b"v=0e e=", true) {
        start = at + 1;
        if !boundary(trace, at) {
            continue;
        }
        if let Some((fault, end)) = kernel_fault_at(trace, at + 7) {
            found.push(fault);
            start = end;
        }
    }
    found
}

// ====== kernel_fault_at ======

fn kernel_fault_at(trace: &[u8], at: usize) -> Option<(KernelFault, usize)> {
    let (after_error, error) = take_hex(trace, at)?;
    let after_cpl = take_prefix(trace, after_error, b" i=0 cpl=0 ip=")?;
    let (after_selector, _) = take_hex(trace, after_cpl)?;
    let after_colon = take_prefix(trace, after_selector, b":")?;
    let (after_rip, rip) = take_hex(trace, after_colon)?;
    let end = line_end(trace, after_rip);
    let mut cursor = end;
    while cursor > after_rip {
        cursor -= 1;
        if take_prefix(trace, cursor, b"cr2=").is_some() && boundary(trace, cursor) {
            if let Some((after_cr2, cr2)) = take_hex(trace, cursor + 4) {
                if after_cr2 <= end {
                    return Some((KernelFault { error, rip, cr2 }, after_cr2));
                }
            }
        }
    }
    None
}

// ====== triple_fault ======

pub fn triple_fault(trace: &[u8]) -> bool {
    find_from(trace, 0, b"triple fault", true).is_some()
}
