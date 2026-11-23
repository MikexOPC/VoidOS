#![no_std]
#![no_main]

mod components;

use components::background::draw_background;
use components::cursor::{CURSOR_H, CURSOR_W, draw_cursor};
use components::dock::DockLayer;
use components::overlay::draw_overlay;
use components::window::{MAX_WINDOWS, WindowSlot};
use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::draw::{Canvas, DirtyList, Rect};
use libui::style::Theme;

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

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
    draw_background(canvas, theme);
    for win in windows {
        win.draw(canvas, clip, theme, now_ms);
    }
    dock.draw(canvas, clip, theme, now_ms);
    draw_overlay(canvas, clip, theme, overlay_text);
    draw_cursor(canvas, clip, cursor_x, cursor_y);
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

    let mut canvas = Canvas::new(pixels, acq.width, acq.height, acq.stride);
    compose_layers(&mut canvas, full, &theme, &windows, &dock, b"VoidOS GUI Ready", cursor_x, cursor_y, 0);
    dirty.add(full, full);
    if !present_dirty(&dirty) {
        libvoid::exit(81);
    }
    dirty.clear();

    if mode == 1 {
        let top_bg = canvas.pixels[0];
        let win_px = canvas.pixels[(100 * acq.stride as usize) + 100];
        if top_bg == 0 || win_px != theme.window_bg {
            libvoid::exit(82);
        }
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
