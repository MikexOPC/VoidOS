#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::draw::{Canvas, Rect};
use libui::proto::{GuiMessage, MSG_INPUT_KEY, MSG_RESIZE_WINDOW, PROTO_WIRE_BYTES};
use libui::style::{Theme, rgb};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const WIN_W: u32 = 680;
const WIN_H: u32 = 368;
const MAX_COLS: usize = 144;
const MAX_ROWS: usize = 40;
const CELL_W: i32 = 9;
const CELL_H: i32 = 18;
const PAD_X: i32 = 14;
const PAD_Y: i32 = 10;
const LINE_CAP: usize = 128;

const CONSOLE_BG: u32 = rgb(0x0B, 0x10, 0x16);
const CONSOLE_FG: u32 = rgb(0xE6, 0xED, 0xF3);
const CONSOLE_CUR: u32 = rgb(0xE6, 0xED, 0xF3);

// ====== TerminalBuffer ======

struct TerminalBuffer {
    cells: [[u8; MAX_COLS]; MAX_ROWS],
    prev_cells: [[u8; MAX_COLS]; MAX_ROWS],
    cols: usize,
    rows: usize,
    row: usize,
    col: usize,
    prev_row: usize,
    prev_col: usize,
    full_dirty: bool,
    input: [u8; LINE_CAP],
    input_len: usize,
}

static mut TERM_STATE: TerminalBuffer = TerminalBuffer::new();

impl TerminalBuffer {
    const fn new() -> Self {
        Self {
            cells: [[b' '; MAX_COLS]; MAX_ROWS],
            prev_cells: [[b' '; MAX_COLS]; MAX_ROWS],
            cols: 72,
            rows: 19,
            row: 0,
            col: 0,
            prev_row: 0,
            prev_col: 0,
            full_dirty: true,
            input: [0u8; LINE_CAP],
            input_len: 0,
        }
    }

    fn resize(&mut self, width: u32, height: u32) {
        let new_cols = (((width as i32) - 2 * PAD_X) / CELL_W).clamp(20, MAX_COLS as i32) as usize;
        let new_rows = (((height as i32) - 2 * PAD_Y) / CELL_H).clamp(6, MAX_ROWS as i32) as usize;
        if new_rows < self.rows && self.row >= new_rows {
            let shift = self.row + 1 - new_rows;
            for r in shift..self.rows {
                self.cells[r - shift] = self.cells[r];
            }
            for r in (self.rows - shift)..self.rows {
                self.cells[r] = [b' '; MAX_COLS];
            }
            self.row = new_rows - 1;
        }
        self.cols = new_cols;
        self.rows = new_rows;
        if self.col >= self.cols {
            self.col = self.cols - 1;
        }
        self.full_dirty = true;
    }

    fn clear(&mut self) {
        for r in 0..MAX_ROWS {
            self.cells[r] = [b' '; MAX_COLS];
        }
        self.row = 0;
        self.col = 0;
        self.full_dirty = true;
    }

    fn newline(&mut self) {
        self.col = 0;
        if self.row + 1 < self.rows {
            self.row += 1;
        } else {
            for r in 1..self.rows {
                self.cells[r - 1] = self.cells[r];
            }
            self.cells[self.rows - 1] = [b' '; MAX_COLS];
            self.full_dirty = true;
        }
    }

    fn write_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            if b == b'\n' {
                self.newline();
            } else if b >= 0x20 && b < 0x7F {
                if self.col >= self.cols {
                    self.newline();
                }
                self.cells[self.row][self.col] = b;
                self.col += 1;
            }
        }
    }

    fn write_prompt(&mut self) {
        self.write_bytes(b"> ");
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
            self.write_bytes(b"SHELL_HELP: help | print <testo> | fl [dir] | rp <file> | write <file> <testo> | rm <file> | sync | clear | exit\n");
            return true;
        }
        if cmd == b"print" {
            self.write_bytes(rest);
            self.write_bytes(b"\n");
            return true;
        }
        if cmd == b"fl" {
            let path = if rest.is_empty() { b"/" as &[u8] } else { rest };
            let mut buf = [0u8; 1024];
            let n = libvoid::list_dir(path, &mut buf);
            if n > 0 {
                self.write_bytes(&buf[..(n as usize).min(buf.len())]);
                if buf[(n as usize).min(buf.len()) - 1] != b'\n' {
                    self.write_bytes(b"\n");
                }
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
        if cmd == b"write" {
            let (path, text) = libvoid::split_first_token(rest);
            if path.is_empty() {
                self.write_bytes(b"write: <file> <testo>\n");
                return true;
            }
            let fd = libvoid::open(path, libvoid::O_WRONLY | libvoid::O_CREAT | libvoid::O_TRUNC);
            if fd <= 0 {
                self.write_bytes(b"write: open failed\n");
                return true;
            }
            let w = libvoid::write_file(fd as u64, text);
            let _ = libvoid::close(fd as u64);
            if w >= 0 {
                self.write_bytes(b"SHELL_WRITE_OK\n");
            } else {
                self.write_bytes(b"write: failed\n");
            }
            return true;
        }
        if cmd == b"rm" {
            if rest.is_empty() {
                self.write_bytes(b"rm: path required\n");
                return true;
            }
            if libvoid::unlink(rest) == 0 {
                self.write_bytes(b"SHELL_RM_OK\n");
            } else {
                self.write_bytes(b"rm: failed\n");
            }
            return true;
        }
        if cmd == b"sync" {
            if libvoid::sync_fs() == 0 {
                self.write_bytes(b"SHELL_SYNC_OK\n");
            } else {
                self.write_bytes(b"sync: failed\n");
            }
            return true;
        }
        self.write_bytes(b"SHELL_UNKNOWN_CMD\n");
        true
    }

    fn cell_rect(r: usize, c: usize) -> Rect {
        Rect::new(PAD_X + (c as i32) * CELL_W, PAD_Y + (r as i32) * CELL_H, CELL_W, CELL_H)
    }

    fn draw_caret(canvas: &mut Canvas<'_>, r: usize, c: usize) {
        let cx = PAD_X + (c as i32) * CELL_W;
        let cy = PAD_Y + (r as i32) * CELL_H;
        canvas.fill_rect(Rect::new(cx, cy + CELL_H - 3, CELL_W - 1, 2), CONSOLE_CUR);
    }

    fn render(&self, canvas: &mut Canvas<'_>, _theme: &Theme, cur_w: u32, cur_h: u32) {
        canvas.fill_rect(Rect::new(0, 0, cur_w as i32, cur_h as i32), CONSOLE_BG);
        for r in 0..self.rows {
            let y = PAD_Y + (r as i32) * CELL_H;
            canvas.draw_text_aa(PAD_X, y, &self.cells[r][..self.cols], CONSOLE_FG, CONSOLE_BG);
        }
        Self::draw_caret(canvas, self.row.min(self.rows - 1), self.col.min(self.cols - 1));
    }

    fn render_dirty(&mut self, canvas: &mut Canvas<'_>, theme: &Theme, cur_w: u32, cur_h: u32) -> Rect {
        let cur_r = self.row.min(self.rows - 1);
        let cur_c = self.col.min(self.cols - 1);
        if self.full_dirty {
            self.render(canvas, theme, cur_w, cur_h);
            for r in 0..self.rows {
                self.prev_cells[r][..self.cols].copy_from_slice(&self.cells[r][..self.cols]);
            }
            self.prev_row = cur_r;
            self.prev_col = cur_c;
            self.full_dirty = false;
            return Rect::new(0, 0, cur_w as i32, cur_h as i32);
        }
        let mut dirty = Rect::new(0, 0, 0, 0);
        if self.prev_row != cur_r || self.prev_col != cur_c {
            let pr = self.prev_row.min(self.rows - 1);
            let pc = self.prev_col.min(self.cols - 1);
            let rect = Self::cell_rect(pr, pc);
            let ch = self.cells[pr][pc];
            canvas.draw_char_aa(rect.x, rect.y, ch, CONSOLE_FG, CONSOLE_BG);
            self.prev_cells[pr][pc] = ch;
            dirty = dirty.union(rect);
        }
        for r in 0..self.rows {
            for c in 0..self.cols {
                let ch = self.cells[r][c];
                if ch != self.prev_cells[r][c] {
                    let rect = Self::cell_rect(r, c);
                    canvas.draw_char_aa(rect.x, rect.y, ch, CONSOLE_FG, CONSOLE_BG);
                    self.prev_cells[r][c] = ch;
                    dirty = dirty.union(rect);
                }
            }
        }
        let cur_rect = Self::cell_rect(cur_r, cur_c);
        canvas.draw_char_aa(cur_rect.x, cur_rect.y, self.cells[cur_r][cur_c], CONSOLE_FG, CONSOLE_BG);
        Self::draw_caret(canvas, cur_r, cur_c);
        self.prev_row = cur_r;
        self.prev_col = cur_c;
        dirty = dirty.union(cur_rect);
        dirty
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

// ====== pages_for_surface ======

fn pages_for_surface(width: u32, height: u32) -> u64 {
    ((width as u64).saturating_mul(height as u64).saturating_mul(4) + 4095) / 4096
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(arg: u64) -> ! {
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
    let mut cur_w = WIN_W;
    let mut cur_h = WIN_H;
    let mut shm_cap_pages = pages_for_surface(cur_w, cur_h);
    let created = libvoid::shm_create(shm_cap_pages);
    if created <= 0 {
        libvoid::exit(91);
    }
    let mut shm_id = created as u64;
    let mapped = libvoid::shm_map(shm_id);
    if mapped < 0x1000_0000 {
        libvoid::exit(92);
    }
    let mut virt = mapped as u64;

    let term = unsafe { &mut *core::ptr::addr_of_mut!(TERM_STATE) };
    term.resize(cur_w, cur_h);

    if arg == 1 {
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        term.write_prompt();
        term.write_bytes(b"print gui_term_ok\n");
        let _ = term.exec_line(b"print gui_term_ok");
        term.write_prompt();
        term.write_bytes(b"fl /\n");
        let _ = term.exec_line(b"fl /");
        term.write_prompt();
        term.write_bytes(b"rp /HELLO.TXT\n");
        let _ = term.exec_line(b"rp /HELLO.TXT");
        term.render(&mut canvas, &theme, cur_w, cur_h);

        let mut lit_pixels = 0u32;
        for &px in canvas.pixels.iter() {
            if px != CONSOLE_BG {
                lit_pixels += 1;
            }
        }
        if lit_pixels < 100 {
            libvoid::exit(93);
        }
        if libvoid::shm_unmap(shm_id) != 0 {
            libvoid::exit(94);
        }
        let _ = libvoid::write_console(b"VoidOS gui terminal ok\n");
        libvoid::exit(0);
    }

    let ipc_handle = arg & !(1u64 << 63);
    if libvoid::shm_publish(shm_id) != 0 {
        libvoid::exit(95);
    }
    let create = GuiMessage::create_window(cur_w, cur_h, shm_id, b"Terminal");
    send_message(ipc_handle, &create.encode());
    term.write_prompt();
    {
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        let initial_dirty = term.render_dirty(&mut canvas, &theme, cur_w, cur_h);
        let initial_commit = GuiMessage::commit_rect(
            1,
            initial_dirty.x.max(0) as u32,
            initial_dirty.y.max(0) as u32,
            initial_dirty.w as u32,
            initial_dirty.h as u32,
        )
        .encode();
        send_message(ipc_handle, &initial_commit);
    }

    let mut rx = [0u8; PROTO_WIRE_BYTES];
    loop {
        let n = libvoid::ipc_recv(ipc_handle, &mut rx);
        if n == PROTO_WIRE_BYTES as i64 {
            if let Some(msg) = GuiMessage::decode(&rx) {
                if msg.kind == MSG_RESIZE_WINDOW {
                    let req_w = msg.e.clamp(240, 2048);
                    let req_h = msg.f.clamp(160, 2048);
                    if req_w == cur_w && req_h == cur_h {
                        continue;
                    }
                    let need_pages = pages_for_surface(req_w, req_h);
                    if need_pages > shm_cap_pages {
                        let next_id = libvoid::shm_create(need_pages);
                        if next_id <= 0 {
                            continue;
                        }
                        let next_virt = libvoid::shm_map(next_id as u64);
                        if next_virt < 0x1000_0000 {
                            let _ = libvoid::shm_unmap(next_id as u64);
                            continue;
                        }
                        if libvoid::shm_publish(next_id as u64) != 0 {
                            let _ = libvoid::shm_unmap(next_id as u64);
                            continue;
                        }
                        let _ = libvoid::shm_unmap(shm_id);
                        shm_id = next_id as u64;
                        virt = next_virt as u64;
                        shm_cap_pages = need_pages;
                    }
                    cur_w = req_w;
                    cur_h = req_h;
                    term.resize(cur_w, cur_h);
                    let reply = GuiMessage::resize_window(1, cur_w, cur_h, shm_id).encode();
                    send_message(ipc_handle, &reply);
                    let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                    let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                    let dirty = term.render_dirty(&mut canvas, &theme, cur_w, cur_h);
                    if !dirty.is_empty() {
                        let commit = GuiMessage::commit_rect(
                            1,
                            dirty.x.max(0) as u32,
                            dirty.y.max(0) as u32,
                            dirty.w as u32,
                            dirty.h as u32,
                        )
                        .encode();
                        send_message(ipc_handle, &commit);
                    }
                } else if msg.kind == MSG_INPUT_KEY {
                    if msg.e & 0xffff_0000 == 0x0002_0000
                        || msg.f == 0
                        || msg.flags & libvoid::INPUT_FLAG_RESYNC != 0
                    {
                        continue;
                    }
                    let ch = (msg.f & 0xFF) as u8;
                    if ch == b'\n' || ch == b'\r' {
                        term.newline();
                        let len = term.input_len;
                        let mut cmd_copy = [0u8; LINE_CAP];
                        cmd_copy[..len].copy_from_slice(&term.input[..len]);
                        term.input_len = 0;
                        if !term.exec_line(&cmd_copy[..len]) {
                            let _ = libvoid::shm_unmap(shm_id);
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
                    let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                    let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                    let dirty = term.render_dirty(&mut canvas, &theme, cur_w, cur_h);
                    if !dirty.is_empty() {
                        let commit = GuiMessage::commit_rect(
                            1,
                            dirty.x.max(0) as u32,
                            dirty.y.max(0) as u32,
                            dirty.w as u32,
                            dirty.h as u32,
                        )
                        .encode();
                        send_message(ipc_handle, &commit);
                    }
                }
            }
        } else if n == libvoid::ERR_WOULD_BLOCK {
            let wait = libvoid::ipc_wait_readable(ipc_handle);
            if wait == libvoid::ERR_PEER_CLOSED {
                libvoid::exit(0);
            }
            if wait < 0 && wait != libvoid::ERR_WOULD_BLOCK {
                libvoid::exit(96);
            }
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
