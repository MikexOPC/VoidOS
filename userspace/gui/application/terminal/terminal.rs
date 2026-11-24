#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::draw::{Canvas, Rect};
use libui::proto::{GuiMessage, MSG_INPUT_KEY, PROTO_WIRE_BYTES};
use libui::style::{Theme, rgb};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const WIN_W: u32 = 480;
const WIN_H: u32 = 280;
const SHM_PAGES: u64 = ((WIN_W as u64 * WIN_H as u64 * 4) + 4095) / 4096;
const COLS: usize = 56;
const ROWS: usize = 15;
const LINE_CAP: usize = 128;

// ====== TerminalBuffer ======

struct TerminalBuffer {
    cells: [[u8; COLS]; ROWS],
    row: usize,
    col: usize,
    input: [u8; LINE_CAP],
    input_len: usize,
}

impl TerminalBuffer {
    const fn new() -> Self {
        Self {
            cells: [[b' '; COLS]; ROWS],
            row: 0,
            col: 0,
            input: [0u8; LINE_CAP],
            input_len: 0,
        }
    }

    fn clear(&mut self) {
        for r in 0..ROWS {
            for c in 0..COLS {
                self.cells[r][c] = b' ';
            }
        }
        self.row = 0;
        self.col = 0;
    }

    fn newline(&mut self) {
        self.col = 0;
        if self.row + 1 < ROWS {
            self.row += 1;
        } else {
            for r in 1..ROWS {
                self.cells[r - 1] = self.cells[r];
            }
            self.cells[ROWS - 1] = [b' '; COLS];
        }
    }

    fn write_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            if b == b'\n' {
                self.newline();
            } else if b >= 0x20 && b < 0x7F {
                if self.col >= COLS {
                    self.newline();
                }
                self.cells[self.row][self.col] = b;
                self.col += 1;
            }
        }
    }

    fn write_prompt(&mut self) {
        self.write_bytes(b"void> ");
    }

    fn exec_line(&mut self, line: &[u8]) -> bool {
        let (cmd, rest) = libvoid::split_first_token(line);
        if cmd.is_empty() {
            return true;
        }
        if cmd == b"exit" {
            return false;
        }
        if cmd == b"clear" {
            self.clear();
            return true;
        }
        if cmd == b"help" {
            self.write_bytes(b"cmds: print fl rp clear exit\n");
            return true;
        }
        if cmd == b"print" {
            self.write_bytes(rest);
            self.write_bytes(b"\n");
            return true;
        }
        if cmd == b"fl" {
            let path = if rest.is_empty() { b"/" as &[u8] } else { rest };
            let mut buf = [0u8; 256];
            let n = libvoid::list_dir(path, &mut buf);
            if n > 0 {
                self.write_bytes(&buf[..(n as usize).min(buf.len())]);
                self.write_bytes(b"\n");
            } else {
                self.write_bytes(b"fl: err\n");
            }
            return true;
        }
        if cmd == b"rp" {
            if rest.is_empty() {
                self.write_bytes(b"rp: path required\n");
                return true;
            }
            let fd = libvoid::open(rest, libvoid::O_RDONLY);
            if fd <= 0 {
                self.write_bytes(b"rp: open failed\n");
                return true;
            }
            let mut buf = [0u8; 256];
            let n = libvoid::read(fd as u64, &mut buf);
            let closed = libvoid::close(fd as u64);
            if n < 0 || closed != 0 {
                self.write_bytes(b"rp: read failed\n");
                return true;
            }
            if n > 0 {
                self.write_bytes(&buf[..(n as usize).min(buf.len())]);
                if buf[(n as usize).min(buf.len()) - 1] != b'\n' {
                    self.write_bytes(b"\n");
                }
                let _ = libvoid::write_console(b"VoidOS terminal VFS read ok\n");
            }
            return true;
        }
        self.write_bytes(b"unknown cmd\n");
        true
    }

    fn render(&self, canvas: &mut Canvas<'_>, theme: &Theme) {
        let bg = rgb(0x0E, 0x13, 0x1C);
        canvas.fill_rect(Rect::new(0, 0, WIN_W as i32, WIN_H as i32), bg);
        for r in 0..ROWS {
            let y = 8 + (r as i32) * 18;
            canvas.draw_text(10, y, &self.cells[r], theme.text_primary);
        }
        let cx = 10 + (self.col.min(COLS - 1) as i32) * 8;
        let cy = 8 + (self.row.min(ROWS - 1) as i32) * 18;
        canvas.fill_rect(Rect::new(cx, cy, 8, 16), theme.accent);
    }
}

// ====== load_theme ======

fn load_theme() -> Theme {
    let fd = libvoid::open(b"/ETC/UI/THEME.VSS", libvoid::O_RDONLY);
    if fd <= 0 {
        return Theme::void_dark();
    }
    let mut buf = [0u8; 1024];
    let n = libvoid::read(fd as u64, &mut buf);
    let _ = libvoid::close(fd as u64);
    if n > 0 {
        Theme::parse_vss(&buf[..(n as usize).min(buf.len())])
    } else {
        Theme::void_dark()
    }
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(arg: u64) -> ! {
    // Isolation invariant: a GUI app must never hold direct display ownership.
    let mut forbidden = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut forbidden) >= 0 {
        libvoid::exit(90);
    }

    let mut display = libvoid::DisplayInfo::default();
    if libvoid::display_info(&mut display) != 0 {
        libvoid::exit(98);
    }
    if display.backbuffer_shm_id != 0 && libvoid::shm_map(display.backbuffer_shm_id) >= 0 {
        libvoid::exit(98);
    }
    let theme = load_theme();
    let shm_id = libvoid::shm_create(SHM_PAGES);
    if shm_id <= 0 {
        libvoid::exit(91);
    }
    let virt = libvoid::shm_map(shm_id as u64);
    if virt < 0x1000_0000 {
        libvoid::exit(92);
    }
    let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (WIN_W * WIN_H) as usize) };
    let mut canvas = Canvas::new(pixels, WIN_W, WIN_H, WIN_W);
    let mut term = TerminalBuffer::new();
    term.write_bytes(b"VoidOS Terminal\n");

    // Mode 1: self-verification pass (real VFS commands + real pixel rendering into SHM).
    if arg == 1 {
        term.write_prompt();
        term.write_bytes(b"print gui_term_ok\n");
        let _ = term.exec_line(b"print gui_term_ok");
        term.write_prompt();
        term.write_bytes(b"fl /\n");
        let _ = term.exec_line(b"fl /");
        term.write_prompt();
        term.write_bytes(b"rp /HELLO.TXT\n");
        let _ = term.exec_line(b"rp /HELLO.TXT");
        term.render(&mut canvas, &theme);

        // Verify glyphs were actually rasterized into the SHM pixel buffer.
        let bg = rgb(0x0E, 0x13, 0x1C);
        let mut lit_pixels = 0u32;
        for &px in canvas.pixels.iter() {
            if px != bg {
                lit_pixels += 1;
            }
        }
        if lit_pixels < 100 {
            libvoid::exit(93);
        }
        if libvoid::shm_unmap(shm_id as u64) != 0 {
            libvoid::exit(94);
        }
        let _ = libvoid::write_console(b"VoidOS gui terminal ok\n");
        libvoid::exit(0);
    }

    // Live IPC window mode: arg carries the IPC channel handle to the compositor.
    let ipc_handle = arg & !(1u64 << 63);
    if libvoid::shm_publish(shm_id as u64) != 0 {
        libvoid::exit(95);
    }
    let create = GuiMessage::create_window(WIN_W, WIN_H, shm_id as u64, b"Terminal");
    let wire = create.encode();
    send_message(ipc_handle, &wire);
    term.write_prompt();
    term.render(&mut canvas, &theme);
    let commit = GuiMessage::commit_rect(1, 0, 0, WIN_W, WIN_H).encode();
    send_message(ipc_handle, &commit);

    let mut rx = [0u8; PROTO_WIRE_BYTES];
    loop {
        let n = libvoid::ipc_recv(ipc_handle, &mut rx);
        if n == PROTO_WIRE_BYTES as i64 {
            if let Some(msg) = GuiMessage::decode(&rx) {
                if msg.kind == MSG_INPUT_KEY {
                    let ch = (msg.f & 0xFF) as u8;
                    if ch == b'\n' || ch == b'\r' {
                        term.newline();
                        let len = term.input_len;
                        let mut cmd_copy = [0u8; LINE_CAP];
                        cmd_copy[..len].copy_from_slice(&term.input[..len]);
                        term.input_len = 0;
                        if !term.exec_line(&cmd_copy[..len]) {
                            let _ = libvoid::shm_unmap(shm_id as u64);
                            libvoid::exit(0);
                        }
                        let _ = libvoid::write_console(b"VoidOS terminal command processed\n");
                        term.write_prompt();
                    } else if ch == 8 {
                        if term.input_len > 0 {
                            term.input_len -= 1;
                            if term.col > 0 {
                                term.col -= 1;
                                term.cells[term.row][term.col] = b' ';
                            }
                        }
                    } else if ch >= 0x20 && ch < 0x7F && term.input_len < LINE_CAP {
                        term.input[term.input_len] = ch;
                        term.input_len += 1;
                        term.write_bytes(&[ch]);
                    }
                    term.render(&mut canvas, &theme);
                    send_message(ipc_handle, &commit);
                }
            }
        } else if n == libvoid::ERR_WOULD_BLOCK {
            libvoid::yield_now();
        } else {
            libvoid::exit(96);
        }
    }
}

// ====== send_message ======

fn send_message(handle: u64, wire: &[u8]) {
    loop {
        let result = libvoid::ipc_send(handle, wire);
        if result == wire.len() as i64 {
            return;
        }
        if result != libvoid::ERR_WOULD_BLOCK {
            libvoid::exit(97);
        }
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
