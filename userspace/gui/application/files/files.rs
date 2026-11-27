#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::draw::{Canvas, Rect};
use libui::proto::{
    GuiMessage, MAX_TITLE_BYTES, MSG_INPUT_KEY, MSG_INPUT_SCROLL, MSG_RESIZE_WINDOW,
    PROTO_WIRE_BYTES,
};
use libui::style::{Theme, rgb};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const WIN_W: u32 = 460;
const WIN_H: u32 = 300;
const MAX_ENTRIES: usize = 32;

struct Entry {
    name: [u8; 40],
    len: usize,
    is_dir: bool,
}

impl Entry {
    const fn empty() -> Self {
        Self {
            name: [0; 40],
            len: 0,
            is_dir: false,
        }
    }
}

struct FileBrowser {
    path: [u8; 64],
    path_len: usize,
    entries: [Entry; MAX_ENTRIES],
    count: usize,
    selected: usize,
    preview: [u8; 96],
    preview_len: usize,
}

impl FileBrowser {
    fn new() -> Self {
        let mut fb = Self {
            path: [0; 64],
            path_len: 1,
            entries: [const { Entry::empty() }; MAX_ENTRIES],
            count: 0,
            selected: 0,
            preview: [0; 96],
            preview_len: 0,
        };
        fb.path[0] = b'/';
        fb.reload();
        fb
    }

    fn set_path(&mut self, p: &[u8]) {
        let len = p.len().min(self.path.len());
        self.path[..len].copy_from_slice(&p[..len]);
        self.path_len = len;
        self.selected = 0;
        self.preview_len = 0;
        self.reload();
    }

    fn reload(&mut self) {
        self.count = 0;
        let mut raw = [0u8; 2048];
        let n = libvoid::list_dir(&self.path[..self.path_len], &mut raw);
        if n <= 0 {
            return;
        }
        let slice = &raw[..(n as usize).min(raw.len())];
        let mut start = 0usize;
        for i in 0..=slice.len() {
            if i == slice.len() || slice[i] == b'\n' {
                if i > start && self.count < MAX_ENTRIES {
                    let item = &slice[start..i];
                    let is_dir = item.last() == Some(&b'/');
                    let clean = if is_dir { &item[..item.len() - 1] } else { item };
                    let len = clean.len().min(self.entries[self.count].name.len());
                    self.entries[self.count].name[..len].copy_from_slice(&clean[..len]);
                    self.entries[self.count].len = len;
                    self.entries[self.count].is_dir = is_dir;
                    self.count += 1;
                }
                start = i + 1;
            }
        }
        if self.selected >= self.count && self.count > 0 {
            self.selected = self.count - 1;
        }
    }

    fn open_selected(&mut self) {
        if self.count == 0 || self.selected >= self.count {
            return;
        }
        let mut full = [0u8; 64];
        let mut flen = self.path_len;
        full[..flen].copy_from_slice(&self.path[..flen]);
        if flen > 0 && full[flen - 1] != b'/' && flen < full.len() {
            full[flen] = b'/';
            flen += 1;
        }
        let ent = &self.entries[self.selected];
        let add = ent.len.min(full.len() - flen);
        full[flen..flen + add].copy_from_slice(&ent.name[..add]);
        flen += add;
        if ent.is_dir {
            self.set_path(&full[..flen]);
        } else {
            self.preview_len = 0;
            let fd = libvoid::open(&full[..flen], libvoid::O_RDONLY);
            if fd > 0 {
                let n = libvoid::read(fd as u64, &mut self.preview);
                let _ = libvoid::close(fd as u64);
                if n > 0 {
                    let len = (n as usize).min(self.preview.len());
                    for b in &mut self.preview[..len] {
                        if *b < 0x20 || *b > 0x7E {
                            *b = b' ';
                        }
                    }
                    self.preview_len = len;
                }
            }
        }
    }

    fn render(&self, canvas: &mut Canvas<'_>, win_w: u32, win_h: u32, theme: &Theme) {
        let w = win_w as i32;
        let h = win_h as i32;
        let footer_y = (h - 40).max(60);

        canvas.fill_rect(Rect::new(0, 0, w, h), theme.window_bg);
        canvas.fill_rect(Rect::new(0, 0, w, 30), rgb(0x1E, 0x29, 0x3B));
        canvas.draw_text(12, 10, b"PATH:", theme.text_muted);
        canvas.draw_text(60, 10, &self.path[..self.path_len], theme.text_primary);
        let hint_x = (w - 156).max(180);
        canvas.draw_text(hint_x, 10, b"[r:/ u:/usb Enter]", theme.text_muted);

        if self.count == 0 {
            canvas.draw_text(16, 48, b"(directory vuota o non montata)", theme.text_muted);
        } else {
            let max_rows = ((footer_y - 40) / 18).max(1) as usize;
            let show_count = self.count.min(max_rows);
            for i in 0..show_count {
                let y = 38 + (i as i32) * 18;
                if i == self.selected {
                    canvas.fill_rect(Rect::new(8, y - 2, (w - 16).max(32), 16), rgb(0x25, 0x63, 0xEB));
                }
                let ent = &self.entries[i];
                let tag = if ent.is_dir { b"[DIR] " } else { b"[FILE]" };
                let tag_col = if ent.is_dir { rgb(0x38, 0xBD, 0xF8) } else { theme.text_muted };
                canvas.draw_text(14, y, tag, tag_col);
                canvas.draw_text(72, y, &ent.name[..ent.len], theme.text_primary);
            }
        }

        canvas.fill_rect(Rect::new(0, footer_y, w, h - footer_y), rgb(0x0F, 0x17, 0x2A));
        canvas.draw_text(12, footer_y + 12, b"PREVIEW:", theme.text_muted);
        if self.preview_len > 0 {
            let max_chars = ((w - 96) / 8).max(8) as usize;
            let show = self.preview_len.min(max_chars);
            canvas.draw_text(84, footer_y + 12, &self.preview[..show], rgb(0x22, 0xC5, 0x5E));
        } else {
            canvas.draw_text(84, footer_y + 12, b"(w/s muovi, Enter apri, Backspace /)", theme.text_muted);
        }
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

// ====== send_message ======

fn send_message(ipc_handle: u64, wire: &[u8; PROTO_WIRE_BYTES]) {
    loop {
        let sent = libvoid::ipc_send(ipc_handle, wire);
        if sent == PROTO_WIRE_BYTES as i64 {
            return;
        }
        if sent == libvoid::ERR_WOULD_BLOCK {
            libvoid::yield_now();
            continue;
        }
        libvoid::exit(0);
    }
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(arg: u64) -> ! {
    let mut forbidden = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut forbidden) >= 0 {
        libvoid::exit(110);
    }

    let theme = load_theme();
    let mut cur_w = WIN_W;
    let mut cur_h = WIN_H;
    let mut shm_cap_pages = pages_for_surface(cur_w, cur_h);
    let created = libvoid::shm_create(shm_cap_pages);
    if created <= 0 {
        libvoid::exit(111);
    }
    let mut shm_id = created as u64;
    let mapped = libvoid::shm_map(shm_id);
    if mapped < 0x1000_0000 {
        libvoid::exit(112);
    }
    let mut virt = mapped as u64;
    let mut browser = FileBrowser::new();

    if arg == 1 {
        if browser.count == 0 {
            libvoid::exit(113);
        }
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        browser.render(&mut canvas, cur_w, cur_h, &theme);
        if libvoid::shm_unmap(shm_id) != 0 {
            libvoid::exit(114);
        }
        let _ = libvoid::write_console(b"VoidOS gui files ok\n");
        libvoid::exit(0);
    }

    let ipc_handle = arg & !(1u64 << 63);
    if libvoid::shm_publish(shm_id) != 0 {
        libvoid::exit(115);
    }
    let create = GuiMessage::create_window(cur_w, cur_h, shm_id, b"Files");
    send_message(ipc_handle, &create.encode());
    {
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        browser.render(&mut canvas, cur_w, cur_h, &theme);
    }
    send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());

    let mut rx = [0u8; PROTO_WIRE_BYTES];
    loop {
        let n = libvoid::ipc_recv(ipc_handle, &mut rx);
        if n == libvoid::ERR_PEER_CLOSED {
            let _ = libvoid::shm_unmap(shm_id);
            libvoid::exit(0);
        }
        if n == PROTO_WIRE_BYTES as i64 {
            if let Some(msg) = GuiMessage::decode(&rx) {
                if msg.kind == MSG_RESIZE_WINDOW {
                    let req_w = msg.e.clamp(240, 2048);
                    let req_h = msg.f.clamp(160, 2048);
                    if req_w != cur_w || req_h != cur_h {
                        let need_pages = pages_for_surface(req_w, req_h);
                        let mut ok = true;
                        if need_pages > shm_cap_pages {
                            let next_id = libvoid::shm_create(need_pages);
                            if next_id > 0 {
                                let next_virt = libvoid::shm_map(next_id as u64);
                                if next_virt >= 0x1000_0000 && libvoid::shm_publish(next_id as u64) == 0 {
                                    let _ = libvoid::shm_unmap(shm_id);
                                    shm_id = next_id as u64;
                                    virt = next_virt as u64;
                                    shm_cap_pages = need_pages;
                                } else {
                                    if next_virt >= 0x1000_0000 {
                                        let _ = libvoid::shm_unmap(next_id as u64);
                                    }
                                    ok = false;
                                }
                            } else {
                                ok = false;
                            }
                        }
                        if ok {
                            cur_w = req_w;
                            cur_h = req_h;
                            send_message(ipc_handle, &GuiMessage::resize_window(1, cur_w, cur_h, shm_id).encode());
                            let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                            let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                            browser.render(&mut canvas, cur_w, cur_h, &theme);
                            send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());
                        }
                    }
                } else if msg.kind == MSG_INPUT_SCROLL {
                    let delta = msg.e as i32;
                    let prev = browser.selected;
                    if delta > 0 {
                        browser.selected = browser.selected.saturating_sub(delta as usize);
                    } else if delta < 0 && browser.count > 0 {
                        let step = (-delta) as usize;
                        browser.selected = (browser.selected + step).min(browser.count - 1);
                    }
                    if browser.selected != prev {
                        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                        browser.render(&mut canvas, cur_w, cur_h, &theme);
                        send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());
                    }
                } else if msg.kind == MSG_INPUT_KEY {
                    if msg.e & 0xffff_0000 == 0x0002_0000
                        || msg.f == 0
                        || msg.flags & libvoid::INPUT_FLAG_RESYNC != 0
                    {
                        continue;
                    }
                    let scalar = msg.f;
                    let mut dirty = true;
                    if scalar == b'w' as u32 || scalar == b'k' as u32 {
                        if browser.selected > 0 {
                            browser.selected -= 1;
                        }
                    } else if scalar == b's' as u32 || scalar == b'j' as u32 {
                        if browser.selected + 1 < browser.count {
                            browser.selected += 1;
                        }
                    } else if scalar == b'u' as u32 {
                        browser.set_path(b"/usb");
                    } else if scalar == b'r' as u32 || scalar == 8 {
                        browser.set_path(b"/");
                    } else if scalar == 10 || scalar == 13 {
                        browser.open_selected();
                    } else if scalar == b'c' as u32 || scalar == 3 {
                        if browser.selected < browser.count {
                            let ent = &browser.entries[browser.selected];
                            let mut full = [0u8; MAX_TITLE_BYTES];
                            let mut pos = 0usize;
                            let plen = browser.path_len.min(MAX_TITLE_BYTES);
                            full[..plen].copy_from_slice(&browser.path[..plen]);
                            pos += plen;
                            if pos > 0 && full[pos - 1] != b'/' && pos < MAX_TITLE_BYTES {
                                full[pos] = b'/';
                                pos += 1;
                            }
                            let nlen = ent.len.min(MAX_TITLE_BYTES - pos);
                            full[pos..pos + nlen].copy_from_slice(&ent.name[..nlen]);
                            pos += nlen;
                            let clip = GuiMessage::clipboard_set(1, &full[..pos]).encode();
                            send_message(ipc_handle, &clip);
                        }
                        dirty = false;
                    } else {
                        dirty = false;
                    }
                    if dirty {
                        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                        browser.render(&mut canvas, cur_w, cur_h, &theme);
                        send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());
                    }
                }
            }
        } else {
            libvoid::yield_now();
        }
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
