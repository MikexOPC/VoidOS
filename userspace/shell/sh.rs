#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call sh_main",
    "ud2",
);

// ====== make_path ======

fn make_path(arg: &[u8], out: &mut [u8; 128]) -> usize {
    let mut pos = 0usize;
    if arg.first() != Some(&b'/') {
        if let Some(slot) = out.get_mut(pos) {
            *slot = b'/';
            pos += 1;
        }
    }
    for &b in arg {
        if pos >= out.len() {
            break;
        }
        if let Some(slot) = out.get_mut(pos) {
            *slot = b;
            pos += 1;
        }
    }
    pos
}

// ====== cmd_fl ======

fn cmd_fl(arg: &[u8]) -> i64 {
    let mut path_buf = [0u8; 128];
    let path = if arg.is_empty() {
        &b"/"[..]
    } else {
        let len = make_path(arg, &mut path_buf);
        path_buf.get(..len).unwrap_or(b"/")
    };
    let mut list_buf = [0u8; 1024];
    let n = libvoid::list_dir(path, &mut list_buf);
    if n < 0 {
        return n;
    }
    let count = n as usize;
    let mut off = 0usize;
    while off < count {
        let end = (off + 256).min(count);
        if let Some(chunk) = list_buf.get(off..end) {
            let _ = libvoid::write_console(chunk);
        }
        off = end;
    }
    0
}

// ====== cmd_rp ======

fn cmd_rp(arg: &[u8]) -> i64 {
    if arg.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(arg, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    let fd = libvoid::open(path, libvoid::O_RDONLY);
    if fd <= 0 {
        return -2;
    }
    let mut data = [0u8; 256];
    let n = libvoid::read(fd as u64, &mut data);
    let closed = libvoid::close(fd as u64);
    if n < 0 || closed != 0 {
        return -3;
    }
    if let Some(slice) = data.get(..n as usize) {
        if !slice.is_empty() {
            let _ = libvoid::write_console(slice);
        }
    }
    0
}

// ====== cmd_write ======

fn cmd_write(arg: &[u8]) -> i64 {
    let (name, body) = libvoid::split_first_token(arg);
    if name.is_empty() || body.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(name, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    let flags = libvoid::O_RDWR | libvoid::O_CREAT | libvoid::O_TRUNC;
    let fd = libvoid::open(path, flags);
    if fd <= 0 {
        return -2;
    }
    let w = libvoid::write_file(fd as u64, body);
    let closed = libvoid::close(fd as u64);
    if w != body.len() as i64 || closed != 0 {
        return -3;
    }
    0
}

// ====== cmd_rm ======

fn cmd_rm(arg: &[u8]) -> i64 {
    if arg.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(arg, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    libvoid::unlink(path)
}

// ====== cmd_mkdir ======

fn cmd_mkdir(arg: &[u8]) -> i64 {
    if arg.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(arg, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    libvoid::mkdir(path)
}

// ====== cmd_rmdir ======

fn cmd_rmdir(arg: &[u8]) -> i64 {
    if arg.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(arg, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    libvoid::rmdir(path)
}

// ====== cmd_run ======

fn cmd_run(arg: &[u8]) -> i64 {
    if arg.is_empty() {
        return -1;
    }
    let mut path_buf = [0u8; 128];
    let len = make_path(arg, &mut path_buf);
    let path = path_buf.get(..len).unwrap_or(&[]);
    let pid = libvoid::spawn(path, None);
    if pid <= 0 {
        return -2;
    }
    let mut status = libvoid::WaitStatus::default();
    if libvoid::wait_pid(pid as u64, Some(&mut status)) != 0 {
        return -3;
    }
    if status.exit_kind != libvoid::EXIT_KIND_NORMAL || status.exit_code != 0 {
        return -4;
    }
    0
}

// ====== execute_line ======

fn execute_line(line: &[u8]) -> i64 {
    let (cmd, arg) = libvoid::split_first_token(line);
    if cmd.is_empty() {
        return 0;
    }
    if cmd == b"help" {
        let _ = libvoid::write_str("sh: help | print | fl | rp | write | rm | mkdir | rmdir | sync | run | exit\n");
        return 0;
    }
    if cmd == b"print" {
        if !arg.is_empty() {
            let _ = libvoid::write_console(arg);
        }
        return 0;
    }
    if cmd == b"fl" {
        return cmd_fl(arg);
    }
    if cmd == b"rp" {
        return cmd_rp(arg);
    }
    if cmd == b"write" {
        return cmd_write(arg);
    }
    if cmd == b"rm" {
        return cmd_rm(arg);
    }
    if cmd == b"mkdir" {
        return cmd_mkdir(arg);
    }
    if cmd == b"rmdir" {
        return cmd_rmdir(arg);
    }
    if cmd == b"sync" {
        return libvoid::sync_fs();
    }
    if cmd == b"run" {
        return cmd_run(arg);
    }
    if cmd == b"exit" {
        let _ = libvoid::write_str("VoidOS sh exit\n");
        return 1;
    }
    let _ = libvoid::write_str("sh: unknown command\n");
    -10
}

// ====== sh_main ======

#[unsafe(no_mangle)]
extern "C" fn sh_main() -> ! {
    let mut chunk = [0u8; 128];
    let mut line = [0u8; 128];
    let mut line_len = 0usize;
    let mut idle_rounds = 0u32;

    loop {
        let n = libvoid::console_read(&mut chunk);
        if n == libvoid::ERR_WOULD_BLOCK {
            idle_rounds += 1;
            if idle_rounds > 64 {
                libvoid::exit(2);
            }
            libvoid::yield_now();
            continue;
        }
        if n <= 0 {
            libvoid::exit(3);
        }
        idle_rounds = 0;
        let count = n as usize;
        for idx in 0..count {
            let Some(&b) = chunk.get(idx) else { break };
            if b == b'\n' || b == b'\r' {
                let slice = line.get(..line_len).unwrap_or(&[]);
                let rc = execute_line(slice);
                line_len = 0;
                if rc == 1 {
                    libvoid::exit(0);
                }
                if rc < 0 {
                    libvoid::exit((100 - rc) as u64);
                }
            } else if line_len < line.len() {
                if let Some(slot) = line.get_mut(line_len) {
                    *slot = b;
                    line_len += 1;
                }
            }
        }
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
