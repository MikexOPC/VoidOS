#![cfg_attr(not(test), no_std)]

#[cfg(not(test))]
use core::arch::asm;

// ====== syscall numbers ======

pub const SYS_EXIT: u64 = 0;
pub const SYS_WRITE: u64 = 1;
pub const SYS_YIELD: u64 = 2;
pub const SYS_SEND: u64 = 3;
pub const SYS_RECV: u64 = 4;
pub const SYS_WAIT_READABLE: u64 = 5;
pub const SYS_OPEN: u64 = 6;
pub const SYS_READ: u64 = 7;
pub const SYS_WRITE_FILE: u64 = 8;
pub const SYS_CLOSE: u64 = 9;
pub const SYS_UNLINK: u64 = 10;
pub const SYS_SPAWN: u64 = 11;
pub const SYS_WAIT_PID: u64 = 12;
pub const SYS_LIST_DIR: u64 = 13;
pub const SYS_SYNC: u64 = 14;
pub const SYS_CONSOLE_READ: u64 = 15;

// ====== flags and errors ======

pub const O_RDONLY: u64 = 0;
pub const O_WRONLY: u64 = 1;
pub const O_RDWR: u64 = 2;
pub const O_CREAT: u64 = 1 << 8;
pub const O_TRUNC: u64 = 1 << 9;

pub const BUDGET_FLAG_SPAWN: u32 = 1;

pub const ERR_WOULD_BLOCK: i64 = -6;
pub const ERR_PEER_CLOSED: i64 = -7;

pub const EXIT_KIND_NORMAL: u32 = 1;
pub const EXIT_KIND_FAULT: u32 = 2;
pub const EXIT_KIND_BAD_FRAME: u32 = 3;
pub const EXIT_KIND_BUDGET: u32 = 4;
pub const EXIT_KIND_KILLED: u32 = 5;

// ====== ABI structures ======

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SpawnBudget {
    pub max_image_pages: u32,
    pub max_open_files: u32,
    pub cpu_budget_ms: u32,
    pub flags: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SpawnArgs {
    pub initial_arg: u64,
    pub flags: u32,
    pub use_budget: u32,
    pub budget: SpawnBudget,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct WaitStatus {
    pub exit_kind: u32,
    pub reserved: u32,
    pub exit_code: u64,
    pub fault_vector: u64,
}

// ====== raw_syscall ======

#[cfg(not(test))]
#[inline]
pub fn raw_syscall(number: u64, first: u64, second: u64, third: u64) -> i64 {
    let result: i64;
    unsafe {
        // Intel SDM Vol. 2B, SYSCALL: rax=number, rdi/rsi/rdx=args, rcx/r11 clobbered.
        asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") first,
            in("rsi") second,
            in("rdx") third,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result
}

// ====== exit ======

#[cfg(not(test))]
#[inline]
pub fn exit(code: u64) -> ! {
    raw_syscall(SYS_EXIT, code, 0, 0);
    loop {
        core::hint::spin_loop();
    }
}

// ====== write_console ======

#[cfg(not(test))]
#[inline]
pub fn write_console(bytes: &[u8]) -> i64 {
    raw_syscall(SYS_WRITE, bytes.as_ptr() as u64, bytes.len() as u64, 0)
}

// ====== write_str ======

#[cfg(not(test))]
#[inline]
pub fn write_str(text: &str) -> i64 {
    write_console(text.as_bytes())
}

// ====== yield_now ======

#[cfg(not(test))]
#[inline]
pub fn yield_now() -> i64 {
    raw_syscall(SYS_YIELD, 0, 0, 0)
}

// ====== ipc_send ======

#[cfg(not(test))]
#[inline]
pub fn ipc_send(handle: u64, data: &[u8]) -> i64 {
    raw_syscall(SYS_SEND, handle, data.as_ptr() as u64, data.len() as u64)
}

// ====== ipc_recv ======

#[cfg(not(test))]
#[inline]
pub fn ipc_recv(handle: u64, buf: &mut [u8]) -> i64 {
    raw_syscall(SYS_RECV, handle, buf.as_mut_ptr() as u64, buf.len() as u64)
}

// ====== ipc_wait_readable ======

#[cfg(not(test))]
#[inline]
pub fn ipc_wait_readable(handle: u64) -> i64 {
    raw_syscall(SYS_WAIT_READABLE, handle, 0, 0)
}

// ====== open ======

#[cfg(not(test))]
#[inline]
pub fn open(path: &[u8], flags: u64) -> i64 {
    raw_syscall(SYS_OPEN, path.as_ptr() as u64, path.len() as u64, flags)
}

// ====== read ======

#[cfg(not(test))]
#[inline]
pub fn read(fd: u64, buf: &mut [u8]) -> i64 {
    raw_syscall(SYS_READ, fd, buf.as_mut_ptr() as u64, buf.len() as u64)
}

// ====== write_file ======

#[cfg(not(test))]
#[inline]
pub fn write_file(fd: u64, data: &[u8]) -> i64 {
    raw_syscall(SYS_WRITE_FILE, fd, data.as_ptr() as u64, data.len() as u64)
}

// ====== close ======

#[cfg(not(test))]
#[inline]
pub fn close(fd: u64) -> i64 {
    raw_syscall(SYS_CLOSE, fd, 0, 0)
}

// ====== unlink ======

#[cfg(not(test))]
#[inline]
pub fn unlink(path: &[u8]) -> i64 {
    raw_syscall(SYS_UNLINK, path.as_ptr() as u64, path.len() as u64, 0)
}

// ====== spawn ======

#[cfg(not(test))]
#[inline]
pub fn spawn(path: &[u8], args: Option<&SpawnArgs>) -> i64 {
    let ptr = match args {
        Some(a) => (a as *const SpawnArgs) as u64,
        None => 0,
    };
    raw_syscall(SYS_SPAWN, path.as_ptr() as u64, path.len() as u64, ptr)
}

// ====== wait_pid ======

#[cfg(not(test))]
#[inline]
pub fn wait_pid(pid: u64, status: Option<&mut WaitStatus>) -> i64 {
    let ptr = match status {
        Some(s) => (s as *mut WaitStatus) as u64,
        None => 0,
    };
    raw_syscall(SYS_WAIT_PID, pid, ptr, 0)
}

// ====== list_dir ======

#[cfg(not(test))]
#[inline]
pub fn list_dir(path: &[u8], buf: &mut [u8]) -> i64 {
    raw_syscall(
        SYS_LIST_DIR,
        path.as_ptr() as u64,
        ((path.len() as u64) << 32) | (buf.len() as u64 & 0xFFFF_FFFF),
        buf.as_mut_ptr() as u64,
    )
}

// ====== sync_fs ======

#[cfg(not(test))]
#[inline]
pub fn sync_fs() -> i64 {
    raw_syscall(SYS_SYNC, 0, 0, 0)
}

// ====== console_read ======

#[cfg(not(test))]
#[inline]
pub fn console_read(buf: &mut [u8]) -> i64 {
    raw_syscall(SYS_CONSOLE_READ, buf.as_mut_ptr() as u64, buf.len() as u64, 0)
}

// ====== trim_spaces ======

pub fn trim_spaces(mut bytes: &[u8]) -> &[u8] {
    while let Some(&first) = bytes.first() {
        if first == b' ' || first == b'\t' || first == b'\r' || first == b'\n' {
            bytes = bytes.get(1..).unwrap_or(&[]);
        } else {
            break;
        }
    }
    while let Some(&last) = bytes.last() {
        if last == b' ' || last == b'\t' || last == b'\r' || last == b'\n' {
            let new_len = bytes.len().saturating_sub(1);
            bytes = bytes.get(..new_len).unwrap_or(&[]);
        } else {
            break;
        }
    }
    bytes
}

// ====== split_first_token ======

pub fn split_first_token(line: &[u8]) -> (&[u8], &[u8]) {
    let trimmed = trim_spaces(line);
    let mut split = trimmed.len();
    for (idx, &b) in trimmed.iter().enumerate() {
        if b == b' ' || b == b'\t' {
            split = idx;
            break;
        }
    }
    let head = trimmed.get(..split).unwrap_or(&[]);
    let tail = trim_spaces(trimmed.get(split..).unwrap_or(&[]));
    (head, tail)
}

// ====== format_u64 ======

pub fn format_u64(mut value: u64, buf: &mut [u8; 20]) -> &[u8] {
    if value == 0 {
        if let Some(slot) = buf.get_mut(19) {
            *slot = b'0';
        }
        return buf.get(19..20).unwrap_or(&[]);
    }
    let mut pos = 20usize;
    while value > 0 && pos > 0 {
        pos -= 1;
        if let Some(slot) = buf.get_mut(pos) {
            *slot = b'0' + (value % 10) as u8;
        }
        value /= 10;
    }
    buf.get(pos..20).unwrap_or(&[])
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::size_of;

    #[test]
    fn abi_struct_sizes() {
        assert_eq!(size_of::<SpawnBudget>(), 16);
        assert_eq!(size_of::<SpawnArgs>(), 32);
        assert_eq!(size_of::<WaitStatus>(), 24);
    }

    #[test]
    fn token_splitting_and_trimming() {
        assert_eq!(split_first_token(b"  ls  /bin  "), (&b"ls"[..], &b"/bin"[..]));
        assert_eq!(split_first_token(b"write note hello world\r\n"), (&b"write"[..], &b"note hello world"[..]));
        assert_eq!(split_first_token(b"   "), (&b""[..], &b""[..]));
        assert_eq!(split_first_token(b"sync"), (&b"sync"[..], &b""[..]));
    }

    #[test]
    fn decimal_formatting() {
        let mut buf = [0u8; 20];
        assert_eq!(format_u64(0, &mut buf), b"0");
        assert_eq!(format_u64(42, &mut buf), b"42");
        assert_eq!(format_u64(18446744073709551615, &mut buf), b"18446744073709551615");
    }
}
