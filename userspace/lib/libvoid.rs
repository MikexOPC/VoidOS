#![cfg_attr(not(test), no_std)]

pub mod heap;

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
pub const SYS_DISPLAY_INFO: u64 = 16;
pub const SYS_DISPLAY_ACQUIRE: u64 = 17;
pub const SYS_DISPLAY_PRESENT: u64 = 18;
pub const SYS_INPUT_POLL: u64 = 19;
pub const SYS_SHM_CREATE: u64 = 20;
pub const SYS_SHM_MAP: u64 = 21;
pub const SYS_SHM_UNMAP: u64 = 22;
pub const SYS_SHM_PUBLISH: u64 = 23;
pub const SYS_SHM_VIEW: u64 = 24;
pub const SYS_PROCESS_STOP: u64 = 25;
pub const SYS_IPC_CLOSE: u64 = 26;
pub const SYS_GUI_WAIT: u64 = 27;
pub const SYS_SYS_INFO: u64 = 28;

// ====== flags and errors ======

pub const O_RDONLY: u64 = 0;
pub const O_WRONLY: u64 = 1;
pub const O_RDWR: u64 = 2;
pub const O_CREAT: u64 = 1 << 8;
pub const O_TRUNC: u64 = 1 << 9;

pub const BUDGET_FLAG_SPAWN: u32 = 1;
pub const BUDGET_FLAG_DISPLAY: u32 = 2;

pub const INPUT_NONE: u32 = 0;
pub const INPUT_KEY: u32 = 1;
pub const INPUT_MOUSE: u32 = 2;
pub const INPUT_FLAG_RESYNC: u32 = 0x8000_0000;
pub const INPUT_FLAG_ABSOLUTE: u32 = 0x4000_0000;

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

// ====== gui_wait ======

#[cfg(not(test))]
#[inline]
pub fn gui_wait(timeout_ms: u64) -> i64 {
    raw_syscall(SYS_GUI_WAIT, timeout_ms, 0, 0)
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

// ====== try_wait_pid ======

#[cfg(not(test))]
#[inline]
pub fn try_wait_pid(pid: u64, status: Option<&mut WaitStatus>) -> i64 {
    let ptr = match status {
        Some(s) => (s as *mut WaitStatus) as u64,
        None => 0,
    };
    raw_syscall(SYS_WAIT_PID, pid, ptr, 0)
}

// ====== wait_pid ======

#[cfg(not(test))]
#[inline]
pub fn wait_pid(pid: u64, mut status: Option<&mut WaitStatus>) -> i64 {
    loop {
        let res = try_wait_pid(pid, status.as_deref_mut());
        if res == ERR_WOULD_BLOCK {
            yield_now();
            continue;
        }
        return res;
    }
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

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DisplayInfo {
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: u32,
    pub uptime_ms: u64,
    pub owner_pid: u64,
    pub backbuffer_shm_id: u64,
    pub rtc_packed: u64,
}

impl DisplayInfo {
    // ====== rtc_date_time ======

    pub fn rtc_date_time(&self) -> Option<(u16, u8, u8, u8, u8, u8)> {
        if (self.rtc_packed & 0xFF) != 1 {
            return None;
        }
        let year = ((self.rtc_packed >> 48) & 0xFFFF) as u16;
        let month = ((self.rtc_packed >> 40) & 0xFF) as u8;
        let day = ((self.rtc_packed >> 32) & 0xFF) as u8;
        let hour = ((self.rtc_packed >> 24) & 0xFF) as u8;
        let minute = ((self.rtc_packed >> 16) & 0xFF) as u8;
        let second = ((self.rtc_packed >> 8) & 0xFF) as u8;
        if year < 2000 || !(1..=12).contains(&month) || !(1..=31).contains(&day)
            || hour > 23 || minute > 59 || second > 59
        {
            return None;
        }
        Some((year, month, day, hour, minute, second))
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DisplayAcquireOut {
    pub shm_id: u64,
    pub user_virt: u64,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DisplayRect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct InputEvent {
    pub kind: u32,
    pub flags: u32,
    pub code: u32,
    pub value: u32,
    pub delta_x: i32,
    pub delta_y: i32,
}

// ====== console_read ======

#[cfg(not(test))]
#[inline]
pub fn console_read(buf: &mut [u8]) -> i64 {
    raw_syscall(SYS_CONSOLE_READ, buf.as_mut_ptr() as u64, buf.len() as u64, 0)
}

// ====== display_info ======

#[cfg(not(test))]
#[inline]
pub fn display_info(out: &mut DisplayInfo) -> i64 {
    raw_syscall(SYS_DISPLAY_INFO, (out as *mut DisplayInfo) as u64, 0, 0)
}

// ====== display_acquire ======

#[cfg(not(test))]
#[inline]
pub fn display_acquire(out: &mut DisplayAcquireOut) -> i64 {
    raw_syscall(SYS_DISPLAY_ACQUIRE, (out as *mut DisplayAcquireOut) as u64, 0, 0)
}

// ====== display_present ======

#[cfg(not(test))]
#[inline]
pub fn display_present(rects: &[DisplayRect]) -> i64 {
    raw_syscall(SYS_DISPLAY_PRESENT, rects.as_ptr() as u64, rects.len() as u64, 0)
}

// ====== input_poll ======

#[cfg(not(test))]
#[inline]
pub fn input_poll(event: &mut InputEvent) -> i64 {
    raw_syscall(SYS_INPUT_POLL, (event as *mut InputEvent) as u64, 0, 0)
}

// ====== shm_create ======

#[cfg(not(test))]
#[inline]
pub fn shm_create(pages: u64) -> i64 {
    raw_syscall(SYS_SHM_CREATE, pages, 0, 0)
}

// ====== shm_map ======

#[cfg(not(test))]
#[inline]
pub fn shm_map(shm_id: u64) -> i64 {
    raw_syscall(SYS_SHM_MAP, shm_id, 0, 0)
}

// ====== shm_unmap ======

#[cfg(not(test))]
#[inline]
pub fn shm_unmap(shm_id: u64) -> i64 {
    raw_syscall(SYS_SHM_UNMAP, shm_id, 0, 0)
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
        assert_eq!(size_of::<DisplayInfo>(), 48);
        assert_eq!(size_of::<DisplayAcquireOut>(), 32);
        assert_eq!(size_of::<DisplayRect>(), 16);
        assert_eq!(size_of::<InputEvent>(), 24);
        assert_eq!(size_of::<SysInfo>(), 64);
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

#[repr(C)]
#[derive(Default)]
pub struct ShmView {
    pub address: u64,
    pub bytes: u64,
}

// ====== shm_publish ======

#[cfg(not(test))]
pub fn shm_publish(shm_id: u64) -> i64 {
    raw_syscall(SYS_SHM_PUBLISH, shm_id, 0, 0)
}

// ====== shm_view ======

#[cfg(not(test))]
pub fn shm_view(shm_id: u64, view: &mut ShmView) -> i64 {
    raw_syscall(SYS_SHM_VIEW, shm_id, view as *mut ShmView as u64, 0)
}

#[repr(C)]
#[derive(Default)]
pub struct ConnectedChild {
    pub pid: u64,
    pub endpoint: u64,
}

// ====== spawn_connected ======

#[cfg(not(test))]
pub fn spawn_connected(path: &[u8], budget: SpawnBudget, child: &mut ConnectedChild) -> i64 {
    let args = SpawnArgs {
        initial_arg: child as *mut ConnectedChild as u64,
        flags: 1,
        use_budget: 1,
        budget,
    };
    spawn(path, Some(&args))
}

// ====== process_stop ======

#[cfg(not(test))]
pub fn process_stop(pid: u64) -> i64 {
    raw_syscall(SYS_PROCESS_STOP, pid, 0, 0)
}

// ====== ipc_close ======

#[cfg(not(test))]
pub fn ipc_close(handle: u64) -> i64 {
    raw_syscall(SYS_IPC_CLOSE, handle, 0, 0)
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SysInfo {
    pub total_phys_pages: u64,
    pub free_phys_pages: u64,
    pub heap_total_pages: u64,
    pub heap_free_pages: u64,
    pub uptime_ms: u64,
    pub preempt_ticks: u64,
    pub active_processes: u32,
    pub online_cpus: u32,
    pub channel_count: u32,
    pub reserved: u32,
}

// ====== sys_info ======

#[cfg(not(test))]
#[inline]
pub fn sys_info(out: &mut SysInfo) -> i64 {
    raw_syscall(SYS_SYS_INFO, out as *mut SysInfo as u64, 0, 0)
}
