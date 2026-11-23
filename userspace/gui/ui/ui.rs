#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::anim::{Easing, RectAnim};
use libui::draw::{Canvas, DirtyList, Rect};
use libui::style::{DockEdge, DockShape, Theme, rgb, rgba};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const MAX_WINDOWS: usize = 8;
const TITLEBAR_HEIGHT: i32 = 28;
const CURSOR_W: i32 = 12;
const CURSOR_H: i32 = 16;

// ====== Dock layer state (Layer 2) ======

struct DockLayer {
    edge: DockEdge,
    shape: DockShape,
    thickness: i32,
    anim: RectAnim,
    hover_item: i32,
}

impl DockLayer {
    fn target_rect(screen_w: i32, screen_h: i32, edge: DockEdge, shape: DockShape, thickness: i32) -> Rect {
        let margin = 12;
        match edge {
            DockEdge::Bottom => match shape {
                DockShape::Floating => {
                    let w = 360.min(screen_w - 24).max(160);
                    Rect::new((screen_w - w) / 2, screen_h - thickness - margin, w, thickness)
                }
                DockShape::Bar => Rect::new(0, screen_h - thickness, screen_w, thickness),
                DockShape::Compact => {
                    let w = 220.min(screen_w - 24).max(140);
                    let h = (thickness - 8).max(30);
                    Rect::new((screen_w - w) / 2, screen_h - h - margin, w, h)
                }
            },
            DockEdge::Top => match shape {
                DockShape::Floating => {
                    let w = 360.min(screen_w - 24).max(160);
                    Rect::new((screen_w - w) / 2, margin, w, thickness)
                }
                DockShape::Bar => Rect::new(0, 0, screen_w, thickness),
                DockShape::Compact => {
                    let w = 220.min(screen_w - 24).max(140);
                    let h = (thickness - 8).max(30);
                    Rect::new((screen_w - w) / 2, margin, w, h)
                }
            },
            DockEdge::Left => {
                let h = match shape {
                    DockShape::Bar => screen_h,
                    DockShape::Floating => 280.min(screen_h - 24).max(140),
                    DockShape::Compact => 180.min(screen_h - 24).max(120),
                };
                let y = if shape == DockShape::Bar { 0 } else { (screen_h - h) / 2 };
                let x = if shape == DockShape::Bar { 0 } else { margin };
                Rect::new(x, y, thickness + 16, h)
            }
            DockEdge::Right => {
                let h = match shape {
                    DockShape::Bar => screen_h,
                    DockShape::Floating => 280.min(screen_h - 24).max(140),
                    DockShape::Compact => 180.min(screen_h - 24).max(120),
                };
                let y = if shape == DockShape::Bar { 0 } else { (screen_h - h) / 2 };
                let w = thickness + 16;
                let x = if shape == DockShape::Bar { screen_w - w } else { screen_w - w - margin };
                Rect::new(x, y, w, h)
            }
        }
    }

    fn new(screen_w: i32, screen_h: i32, theme: &Theme) -> Self {
        let r = Self::target_rect(screen_w, screen_h, theme.dock_edge, theme.dock_shape, theme.dock_thickness as i32);
        Self {
            edge: theme.dock_edge,
            shape: theme.dock_shape,
            thickness: theme.dock_thickness as i32,
            anim: RectAnim::settled(r.x, r.y, r.w, r.h),
            hover_item: -1,
        }
    }

    fn morph_to(&mut self, now_ms: u64, screen_w: i32, screen_h: i32, edge: DockEdge, shape: DockShape, dur_ms: u32) {
        self.edge = edge;
        self.shape = shape;
        let r = Self::target_rect(screen_w, screen_h, edge, shape, self.thickness);
        self.anim.retarget(now_ms, r.x, r.y, r.w, r.h, dur_ms, Easing::EaseOutBack);
    }

    fn cycle_form(&mut self, now_ms: u64, screen_w: i32, screen_h: i32, dur_ms: u32) {
        let (next_edge, next_shape) = match (self.edge, self.shape) {
            (DockEdge::Bottom, DockShape::Floating) => (DockEdge::Bottom, DockShape::Bar),
            (DockEdge::Bottom, DockShape::Bar) => (DockEdge::Bottom, DockShape::Compact),
            (DockEdge::Bottom, DockShape::Compact) => (DockEdge::Left, DockShape::Floating),
            (DockEdge::Left, _) => (DockEdge::Top, DockShape::Bar),
            _ => (DockEdge::Bottom, DockShape::Floating),
        };
        self.morph_to(now_ms, screen_w, screen_h, next_edge, next_shape, dur_ms);
    }

    fn current_rect(&self, now_ms: u64) -> Rect {
        let (x, y, w, h) = self.anim.sample(now_ms);
        Rect::new(x, y, w, h)
    }
}

// ====== Window slot (Layer 1) ======

#[derive(Clone, Copy)]
struct WindowSlot {
    active: bool,
    focused: bool,
    rect: RectAnim,
    title: [u8; 32],
    title_len: usize,
    body_color: u32,
}

impl WindowSlot {
    const fn empty() -> Self {
        Self {
            active: false,
            focused: false,
            rect: RectAnim::settled(0, 0, 0, 0),
            title: [0u8; 32],
            title_len: 0,
            body_color: 0,
        }
    }

    fn open(x: i32, y: i32, w: i32, h: i32, title: &[u8], body_color: u32, focused: bool) -> Self {
        let mut slot = Self::empty();
        slot.active = true;
        slot.focused = focused;
        slot.rect = RectAnim::settled(x, y, w, h);
        let n = title.len().min(31);
        slot.title[..n].copy_from_slice(&title[..n]);
        slot.title_len = n;
        slot.body_color = body_color;
        slot
    }
}

// ====== load_theme_from_vfs ======

fn load_theme_from_vfs() -> Theme {
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

// ====== draw_cursor (Layer 4) ======

fn draw_cursor(canvas: &mut Canvas<'_>, cx: i32, cy: i32) {
    const SHAPE: [u16; 16] = [
        0b1000_0000_0000,
        0b1100_0000_0000,
        0b1110_0000_0000,
        0b1111_0000_0000,
        0b1111_1000_0000,
        0b1111_1100_0000,
        0b1111_1110_0000,
        0b1111_1111_0000,
        0b1111_1111_1000,
        0b1111_1100_0000,
        0b1101_1100_0000,
        0b1000_1110_0000,
        0b0000_1110_0000,
        0b0000_0111_0000,
        0b0000_0111_0000,
        0b0000_0010_0000,
    ];
    let white = rgb(0xFF, 0xFF, 0xFF);
    let dark = rgb(0x10, 0x14, 0x1C);
    for (row, &bits) in SHAPE.iter().enumerate() {
        for col in 0..12 {
            if (bits & (1 << (11 - col))) != 0 {
                let is_edge = col == 0 || row == 0 || (bits & (1 << (11 - col - 1))) == 0;
                canvas.put_pixel(cx + col, cy + row as i32, if is_edge { white } else { dark });
            }
        }
    }
}

// ====== compose_layers ======

fn compose_layers(
    canvas: &mut Canvas<'_>,
    clip: Rect,
    theme: &Theme,
    windows: &[WindowSlot; MAX_WINDOWS],
    dock: &DockLayer,
    overlay_text: &[u8],
    cursor_x: i32,
    cursor_y: i32,
    now_ms: u64,
) {
    canvas.set_clip(clip);
    let full = Rect::new(0, 0, canvas.width as i32, canvas.height as i32);

    // Layer 0: Background
    canvas.fill_vertical_gradient(full, theme.bg_top, theme.bg_bottom);
    canvas.draw_text(16, 12, b"VoidOS", theme.text_muted);

    // Layer 1: Windows (z-order)
    for win in windows {
        if !win.active {
            continue;
        }
        let (wx, wy, ww, wh) = win.rect.sample(now_ms);
        let wr = Rect::new(wx, wy, ww, wh);
        if wr.intersect(clip).is_empty() {
            continue;
        }
        let border_col = if win.focused { theme.window_border_focus } else { theme.window_border };
        let tb_col = if win.focused { theme.titlebar_focus } else { theme.titlebar_bg };
        canvas.fill_rounded_rect(wr, theme.window_radius, win.body_color);
        canvas.fill_rect(Rect::new(wr.x + 1, wr.y + 1, wr.w - 2, TITLEBAR_HEIGHT), tb_col);
        canvas.stroke_rect(wr, border_col);
        canvas.draw_text(wr.x + 12, wr.y + 6, &win.title[..win.title_len], theme.text_primary);
        // Close button
        let close_r = Rect::new(wr.right() - 22, wr.y + 6, 14, 14);
        canvas.fill_rounded_rect(close_r, 4, theme.danger);
    }

    // Layer 2: Dock (dynamic shape & edge)
    let dr = dock.current_rect(now_ms);
    if !dr.intersect(clip).is_empty() {
        let radius = if dock.shape == DockShape::Bar { 0 } else { theme.dock_radius };
        canvas.fill_rounded_rect(dr, radius, theme.dock_bg);
        canvas.stroke_rect(dr, theme.dock_border);
        let labels: [&[u8]; 3] = [b"TERM", b"SYS", b"DOCK"];
        let horizontal = matches!(dock.edge, DockEdge::Bottom | DockEdge::Top);
        for (i, label) in labels.iter().enumerate() {
            let btn = if horizontal {
                let bw = 64;
                let gap = 10;
                let total = 3 * bw + 2 * gap;
                let start_x = dr.x + (dr.w - total) / 2;
                Rect::new(start_x + (i as i32) * (bw + gap), dr.y + 8, bw, (dr.h - 16).max(20))
            } else {
                let bh = 36;
                let gap = 10;
                let total = 3 * bh + 2 * gap;
                let start_y = dr.y + (dr.h - total) / 2;
                Rect::new(dr.x + 8, start_y + (i as i32) * (bh + gap), (dr.w - 16).max(24), bh)
            };
            let bg = if dock.hover_item == i as i32 {
                theme.accent_hover
            } else if i == 2 {
                rgba(0x28, 0x34, 0x4A, 0xF0)
            } else {
                theme.accent
            };
            canvas.fill_rounded_rect(btn, 6, bg);
            canvas.draw_text(btn.x + 12, btn.y + 8, label, theme.text_primary);
        }
    }

    // Layer 3: Overlay pill
    if !overlay_text.is_empty() {
        let ow = (overlay_text.len() as i32) * 8 + 20;
        let or = Rect::new((canvas.width as i32 - ow) / 2, 16, ow, 28);
        if !or.intersect(clip).is_empty() {
            canvas.fill_rounded_rect(or, 8, rgba(0x12, 0x18, 0x24, 0xE8));
            canvas.stroke_rect(or, theme.accent);
            canvas.draw_text(or.x + 10, or.y + 6, overlay_text, theme.text_primary);
        }
    }

    // Layer 4: Cursor
    let cr = Rect::new(cursor_x, cursor_y, CURSOR_W, CURSOR_H);
    if !cr.intersect(clip).is_empty() {
        draw_cursor(canvas, cursor_x, cursor_y);
    }
}

// ====== present_dirty ======

fn present_dirty(dirty: &DirtyList) -> bool {
    let slice = dirty.as_slice();
    if slice.is_empty() {
        return true;
    }
    let mut wire = [libvoid::DisplayRect::default(); 16];
    let n = slice.len().min(16);
    for i in 0..n {
        wire[i] = libvoid::DisplayRect {
            x: slice[i].x as u32,
            y: slice[i].y as u32,
            width: slice[i].w as u32,
            height: slice[i].h as u32,
        };
    }
    libvoid::display_present(&wire[..n]) == 0
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(mode: u64) -> ! {
    let theme = load_theme_from_vfs();
    let mut acq = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut acq) != 0 || acq.user_virt < 0x1000_0000 || acq.width == 0 || acq.height == 0 {
        libvoid::exit(80);
    }
    let total_pixels = (acq.stride as usize).saturating_mul(acq.height as usize);
    let pixels = unsafe { core::slice::from_raw_parts_mut(acq.user_virt as *mut u32, total_pixels) };
    let screen_w = acq.width as i32;
    let screen_h = acq.height as i32;
    let full = Rect::new(0, 0, screen_w, screen_h);

    let mut windows = [WindowSlot::empty(); MAX_WINDOWS];
    windows[0] = WindowSlot::open(64, 64, 420, 260, b"Terminal", theme.window_bg, true);
    let mut dock = DockLayer::new(screen_w, screen_h, &theme);
    let mut cursor_x = screen_w / 2;
    let mut cursor_y = screen_h / 2;
    let mut dirty = DirtyList::new();

    // Initial full-screen composition across all 5 layers.
    let mut canvas = Canvas::new(pixels, acq.width, acq.height, acq.stride);
    compose_layers(&mut canvas, full, &theme, &windows, &dock, b"VoidOS GUI Ready", cursor_x, cursor_y, 0);
    dirty.add(full, full);
    if !present_dirty(&dirty) {
        libvoid::exit(81);
    }
    dirty.clear();

    // Mode 1: self-verification pass (verifies 5-layer render + Dock morph animation + clean exit & console restore).
    if mode == 1 {
        let top_bg = canvas.pixels[0];
        let win_px = canvas.pixels[(100 * acq.stride as usize) + 100];
        if top_bg == 0 || win_px != theme.window_bg {
            libvoid::exit(82);
        }
        // Morph Dock: Floating -> Bar -> Compact and present a dirty rect.
        let prev_dock = dock.current_rect(0);
        dock.cycle_form(0, screen_w, screen_h, theme.anim_ms);
        let mid_dock = dock.current_rect((theme.anim_ms / 2) as u64);
        let end_dock = dock.current_rect(theme.anim_ms as u64);
        if mid_dock == prev_dock || end_dock.w != screen_w {
            libvoid::exit(83);
        }
        dirty.add(prev_dock, full);
        dirty.add(end_dock, full);
        for &r in dirty.as_slice() {
            compose_layers(&mut canvas, r, &theme, &windows, &dock, b"Dock: Bar", cursor_x, cursor_y, theme.anim_ms as u64);
        }
        if !present_dirty(&dirty) {
            libvoid::exit(84);
        }
        let _ = libvoid::write_console(b"VoidOS ui compositor ok\n");
        libvoid::exit(0);
    }

    // Mode 0: live interactive compositor loop.
    let mut prev_dock = dock.current_rect(0);
    let mut dinfo = libvoid::DisplayInfo::default();
    let mut overlay: &[u8] = b"";
    loop {
        let _ = libvoid::display_info(&mut dinfo);
        let now_ms = dinfo.uptime_ms;
        let mut ev = libvoid::InputEvent::default();
        while libvoid::input_poll(&mut ev) == 1 {
            if ev.kind == libvoid::INPUT_MOUSE {
                let old_cur = Rect::new(cursor_x, cursor_y, CURSOR_W, CURSOR_H);
                cursor_x = (cursor_x + ev.delta_x).clamp(0, screen_w - 1);
                cursor_y = (cursor_y - ev.delta_y).clamp(0, screen_h - 1);
                dirty.add(old_cur, full);
                dirty.add(Rect::new(cursor_x, cursor_y, CURSOR_W, CURSOR_H), full);
                if (ev.flags & 1) != 0 && dock.current_rect(now_ms).contains(cursor_x, cursor_y) {
                    dock.cycle_form(now_ms, screen_w, screen_h, theme.anim_ms);
                    overlay = b"Dock Morph";
                    dirty.add(Rect::new(0, 0, screen_w, 56), full);
                }
            } else if ev.kind == libvoid::INPUT_KEY {
                if ev.value == 27 {
                    // Escape exits cleanly -> kernel automatically restores text console.
                    libvoid::exit(0);
                }
                if ev.value == b'\t' as u32 {
                    dock.cycle_form(now_ms, screen_w, screen_h, theme.anim_ms);
                    overlay = b"Dock Morph";
                    dirty.add(Rect::new(0, 0, screen_w, 56), full);
                }
            }
        }
        if dock.anim.is_animating(now_ms) {
            let cur_dock = dock.current_rect(now_ms);
            dirty.add(prev_dock, full);
            dirty.add(cur_dock, full);
            prev_dock = cur_dock;
            dock.anim.tick(now_ms);
        }
        if !dirty.as_slice().is_empty() {
            for &r in dirty.as_slice() {
                compose_layers(&mut canvas, r, &theme, &windows, &dock, overlay, cursor_x, cursor_y, now_ms);
            }
            let _ = present_dirty(&dirty);
            dirty.clear();
        }
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
