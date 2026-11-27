#![no_std]
#![no_main]

extern crate alloc;

mod apps;
mod cursor;
mod cursor_data;

use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::format;
use alloc::rc::Rc;
use core::arch::global_asm;
use core::cell::{Cell, RefCell};
use core::panic::PanicInfo;
use core::time::Duration;
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType, TargetPixel};
use slint::platform::{Platform, PointerEventButton, WindowAdapter, WindowEvent};
use slint::{Image, LogicalPosition, PhysicalSize, Rgb8Pixel, SharedPixelBuffer};

slint::include_modules!();

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

#[global_allocator]
static ALLOCATOR: libvoid::heap::ShmAllocator = libvoid::heap::ShmAllocator::new(4096); // 16 MiB SHM heap

// ====== BgrxPixel (UEFI GOP / VirtIO-GPU native 32-bit pixel) ======

#[repr(transparent)]
#[derive(Clone, Copy, Default)]
pub struct BgrxPixel(pub u32);

impl TargetPixel for BgrxPixel {
    fn blend(&mut self, color: slint::platform::software_renderer::PremultipliedRgbaColor) {
        let a = color.alpha as u32;
        if a == 255 {
            self.0 = (color.blue as u32)
                | ((color.green as u32) << 8)
                | ((color.red as u32) << 16)
                | 0xFF00_0000;
            return;
        }
        if a == 0 {
            return;
        }
        let inv = 255 - a;
        let db = self.0 & 0xFF;
        let dg = (self.0 >> 8) & 0xFF;
        let dr = (self.0 >> 16) & 0xFF;
        let b = (color.blue as u32 + ((db * inv + 128) >> 8)).min(255);
        let g = (color.green as u32 + ((dg * inv + 128) >> 8)).min(255);
        let r = (color.red as u32 + ((dr * inv + 128) >> 8)).min(255);
        self.0 = b | (g << 8) | (r << 16) | 0xFF00_0000;
    }

    fn blend_slice(slice: &mut [Self], color: slint::platform::software_renderer::PremultipliedRgbaColor) {
        let a = color.alpha as u32;
        if a == 0 {
            return;
        }
        if a == 255 {
            let packed = Self(
                (color.blue as u32)
                    | ((color.green as u32) << 8)
                    | ((color.red as u32) << 16)
                    | 0xFF00_0000,
            );
            slice.fill(packed);
            return;
        }
        let inv = 255 - a;
        let cr = color.red as u32;
        let cg = color.green as u32;
        let cb = color.blue as u32;
        for dst in slice.iter_mut() {
            let v = dst.0;
            let b = (cb + (((v & 0xFF) * inv + 128) >> 8)).min(255);
            let g = (cg + ((((v >> 8) & 0xFF) * inv + 128) >> 8)).min(255);
            let r = (cr + ((((v >> 16) & 0xFF) * inv + 128) >> 8)).min(255);
            dst.0 = b | (g << 8) | (r << 16) | 0xFF00_0000;
        }
    }

    fn from_rgb(r: u8, g: u8, b: u8) -> Self {
        Self((b as u32) | ((g as u32) << 8) | ((r as u32) << 16) | 0xFF00_0000)
    }
}

// ====== VoidPlatform (Slint Platform backend on VoidOS syscalls) ======

struct VoidPlatform {
    window: Rc<MinimalSoftwareWindow>,
    start_ms: u64,
    now_ms: Rc<Cell<u64>>,
}

impl Platform for VoidPlatform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
        Ok(self.window.clone())
    }

    fn duration_since_start(&self) -> Duration {
        Duration::from_millis(self.now_ms.get().saturating_sub(self.start_ms))
    }
}

// ====== present_rects ======

fn present_rects(
    region: Option<slint::platform::software_renderer::PhysicalRegion>,
    extra0: Option<libvoid::DisplayRect>,
    extra1: Option<libvoid::DisplayRect>,
    max_w: u32,
    max_h: u32,
) -> bool {
    let bounds = libui::draw::Rect::new(0, 0, max_w as i32, max_h as i32);
    let mut dirty = libui::draw::DirtyList::new();
    if let Some(reg) = region {
        for (origin, size) in reg.iter() {
            dirty.add(
                libui::draw::Rect::new(origin.x, origin.y, size.width as i32, size.height as i32),
                bounds,
            );
        }
    }
    if let Some(r) = extra0 {
        dirty.add(
            libui::draw::Rect::new(r.x as i32, r.y as i32, r.width as i32, r.height as i32),
            bounds,
        );
    }
    if let Some(r) = extra1 {
        dirty.add(
            libui::draw::Rect::new(r.x as i32, r.y as i32, r.width as i32, r.height as i32),
            bounds,
        );
    }
    let rects = dirty.as_slice();
    if rects.is_empty() {
        return true;
    }
    let mut wire = [libvoid::DisplayRect::default(); libui::draw::MAX_DIRTY_RECTS];
    for (idx, r) in rects.iter().enumerate() {
        wire[idx] = libvoid::DisplayRect {
            x: r.x as u32,
            y: r.y as u32,
            width: r.w as u32,
            height: r.h as u32,
        };
    }
    libvoid::display_present(&wire[..rects.len()]) == 0
}

// ====== is_leap_year / days_in_month / day_of_week / utc_to_rome ======

fn is_leap_year(y: u32) -> bool {
    (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)
}

fn days_in_month(y: u32, m: u32) -> u32 {
    match m {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 => if is_leap_year(y) { 29 } else { 28 },
        _ => 30,
    }
}

// Sakamoto's algorithm: 0 = Sunday, 1 = Monday, ..., 6 = Saturday.
fn day_of_week(y: u32, m: u32, d: u32) -> u32 {
    const T: [u32; 12] = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4];
    let yy = if m < 3 { y - 1 } else { y };
    (yy + yy / 4 - yy / 100 + yy / 400 + T[(m - 1) as usize] + d) % 7
}

fn last_sunday_of(y: u32, m: u32) -> u32 {
    let dim = days_in_month(y, m);
    let dow = day_of_week(y, m, dim);
    dim - dow
}

// Europe/Rome timezone: CET (UTC+1) in winter, CEST (UTC+2) from last Sunday of March 01:00 UTC
// to last Sunday of October 01:00 UTC.
fn utc_to_rome(mut year: u32, mut month: u32, mut day: u32, mut hour: u32) -> (u32, u32, u32, u32) {
    let dst_start_day = last_sunday_of(year, 3);
    let dst_end_day = last_sunday_of(year, 10);
    let after_start = month > 3 || (month == 3 && (day > dst_start_day || (day == dst_start_day && hour >= 1)));
    let before_end = month < 10 || (month == 10 && (day < dst_end_day || (day == dst_end_day && hour < 1)));
    let offset = if after_start && before_end { 2 } else { 1 };
    hour += offset;
    if hour >= 24 {
        hour -= 24;
        day += 1;
        if day > days_in_month(year, month) {
            day = 1;
            month += 1;
            if month > 12 {
                month = 1;
                year += 1;
            }
        }
    }
    (year, month, day, hour)
}

// ====== format_rtc_labels ======

fn format_rtc_labels(dinfo: &libvoid::DisplayInfo, width: u32, height: u32) -> (slint::SharedString, slint::SharedString) {
    const MONTHS: [&str; 12] = [
        "Gen", "Feb", "Mar", "Apr", "Mag", "Giu",
        "Lug", "Ago", "Set", "Ott", "Nov", "Dic",
    ];
    if let Some((y_utc, m_utc, d_utc, h_utc, minute, _second)) = dinfo.rtc_date_time() {
        let (year, month, day, hour) = utc_to_rome(y_utc as u32, m_utc as u32, d_utc as u32, h_utc as u32);
        let mon = MONTHS.get((month as usize).saturating_sub(1)).copied().unwrap_or("---");
        (
            format!("{:02}:{:02}", hour, minute).into(),
            format!("{:02} {} {}", day, mon, year).into(),
        )
    } else {
        (
            format!("{} × {}", width, height).into(),
            format!("{} s", dinfo.uptime_ms / 1000).into(),
        )
    }
}

// ====== build_dock_blur ======

// Generates a real 3-pass separable box blur (approx Gaussian radius ~14px) directly from the
// rendered wallpaper strip behind the dock so changing wallpapers always keeps a native blur.
fn build_dock_blur(
    backbuffer: &[BgrxPixel],
    stride: u32,
    width: u32,
    height: u32,
) -> Image {
    let dx = 44u32.min(width);
    let dy = height.saturating_sub(62);
    let dw = width.saturating_sub(88).max(1);
    let dh = 50u32.min(height.saturating_sub(dy)).max(1);
    let n = (dw as usize) * (dh as usize);
    let mut cur = alloc::vec![[0u8; 3]; n];
    let mut tmp = alloc::vec![[0u8; 3]; n];

    for y in 0..dh {
        let sy = (dy + y) as usize;
        let row_off = sy * (stride as usize);
        for x in 0..dw {
            let sx = (dx + x) as usize;
            let px = backbuffer[row_off + sx].0;
            cur[(y as usize) * (dw as usize) + (x as usize)] = [
                ((px >> 16) & 0xFF) as u8,
                ((px >> 8) & 0xFF) as u8,
                (px & 0xFF) as u8,
            ];
        }
    }

    let w = dw as usize;
    let h = dh as usize;
    let r = 10usize;
    let diam = (2 * r + 1) as u32;

    for _pass in 0..3 {
        // Horizontal box pass
        for y in 0..h {
            let row = y * w;
            let mut sr = 0u32;
            let mut sg = 0u32;
            let mut sb = 0u32;
            for k in 0..=r {
                let p = cur[row + k.min(w - 1)];
                sr += p[0] as u32;
                sg += p[1] as u32;
                sb += p[2] as u32;
            }
            let left0 = cur[row];
            sr += (left0[0] as u32) * (r as u32);
            sg += (left0[1] as u32) * (r as u32);
            sb += (left0[2] as u32) * (r as u32);
            for x in 0..w {
                tmp[row + x] = [(sr / diam) as u8, (sg / diam) as u8, (sb / diam) as u8];
                let out_x = x.saturating_sub(r);
                let in_x = (x + r + 1).min(w - 1);
                let po = cur[row + out_x];
                let pi = cur[row + in_x];
                sr = sr + pi[0] as u32 - po[0] as u32;
                sg = sg + pi[1] as u32 - po[1] as u32;
                sb = sb + pi[2] as u32 - po[2] as u32;
            }
        }
        // Vertical box pass
        for x in 0..w {
            let mut sr = 0u32;
            let mut sg = 0u32;
            let mut sb = 0u32;
            for k in 0..=r {
                let p = tmp[k.min(h - 1) * w + x];
                sr += p[0] as u32;
                sg += p[1] as u32;
                sb += p[2] as u32;
            }
            let top0 = tmp[x];
            sr += (top0[0] as u32) * (r as u32);
            sg += (top0[1] as u32) * (r as u32);
            sb += (top0[2] as u32) * (r as u32);
            for y in 0..h {
                cur[y * w + x] = [(sr / diam) as u8, (sg / diam) as u8, (sb / diam) as u8];
                let out_y = y.saturating_sub(r);
                let in_y = (y + r + 1).min(h - 1);
                let po = tmp[out_y * w + x];
                let pi = tmp[in_y * w + x];
                sr = sr + pi[0] as u32 - po[0] as u32;
                sg = sg + pi[1] as u32 - po[1] as u32;
                sb = sb + pi[2] as u32 - po[2] as u32;
            }
        }
    }

    let mut buf = SharedPixelBuffer::<Rgb8Pixel>::new(dw, dh);
    for (dst, src) in buf.make_mut_slice().iter_mut().zip(cur.iter()) {
        *dst = Rgb8Pixel::new(src[0], src[1], src[2]);
    }
    Image::from_rgb8(buf)
}

// ====== cache_screen_wallpaper ======

// Captures the rendered 1:1 screen-sized wallpaper so window drag redraws use direct copy
// instead of per-frame CPU bilinear JPEG resampling.
fn cache_screen_wallpaper(
    backbuffer: &[BgrxPixel],
    stride: u32,
    width: u32,
    height: u32,
) -> Image {
    let mut buf = SharedPixelBuffer::<Rgb8Pixel>::new(width, height);
    let dst = buf.make_mut_slice();
    let w = width as usize;
    let s = stride as usize;
    for y in 0..(height as usize) {
        let src_row = &backbuffer[y * s..y * s + w];
        let dst_row = &mut dst[y * w..y * w + w];
        for (d, p) in dst_row.iter_mut().zip(src_row.iter()) {
            let v = p.0;
            *d = Rgb8Pixel::new(((v >> 16) & 0xFF) as u8, ((v >> 8) & 0xFF) as u8, (v & 0xFF) as u8);
        }
    }
    Image::from_rgb8(buf)
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(mode: u64) -> ! {
    let mut acq = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut acq) != 0
        || acq.user_virt < 0x1000_0000
        || acq.width == 0
        || acq.height == 0
    {
        libvoid::exit(80);
    }

    let mut dinfo = libvoid::DisplayInfo::default();
    if libvoid::display_info(&mut dinfo) != 0 {
        libvoid::exit(86);
    }

    let window = MinimalSoftwareWindow::new(RepaintBufferType::ReusedBuffer);
    window.set_size(PhysicalSize::new(acq.width, acq.height));

    let platform_now = Rc::new(Cell::new(dinfo.uptime_ms));
    if slint::platform::set_platform(Box::new(VoidPlatform {
        window: window.clone(),
        start_ms: dinfo.uptime_ms,
        now_ms: platform_now.clone(),
    }))
    .is_err()
    {
        libvoid::exit(81);
    }

    let ui = match VoidDesktop::new() {
        Ok(u) => u,
        Err(_) => libvoid::exit(82),
    };
    let requests = Rc::new(RefCell::new(VecDeque::new()));
    let launch = requests.clone();
    ui.on_launch_app(move |kind| launch.borrow_mut().push_back((0, kind)));
    let close = requests.clone();
    ui.on_close_app(move |kind| close.borrow_mut().push_back((1, kind)));
    let minimize = requests.clone();
    ui.on_minimize_app(move |kind| minimize.borrow_mut().push_back((3, kind)));
    let maximize = requests.clone();
    ui.on_maximize_app(move |kind| maximize.borrow_mut().push_back((4, kind)));
    let quitting = Rc::new(Cell::new(false));
    let quit = quitting.clone();
    ui.on_quit_desktop(move || quit.set(true));
    let mut applications = apps::Applications::new(mode == 1);
    ui.set_surfaces(applications.model().into());
    applications.refresh(&ui);
    if ui.show().is_err() {
        libvoid::exit(82);
    }
    let (clock_str, date_str) = format_rtc_labels(&dinfo, acq.width, acq.height);
    ui.set_display_label(clock_str);
    ui.set_uptime_label(date_str);

    let mut total_pixels = (acq.stride as usize).saturating_mul(acq.height as usize);
    let mut backbuffer =
        unsafe { core::slice::from_raw_parts_mut(acq.user_virt as *mut BgrxPixel, total_pixels) };

    if mode == 1 || mode == 2 {
        if !applications.launch(0, dinfo.uptime_ms) {
            let _ = libvoid::write_console(b"GUI self-check error 87\n");
            libvoid::exit(87);
        }
        let mut initial_commit = false;
        for _ in 0..10000 {
            if !applications.poll(&ui, acq.width, acq.height) {
                let _ = libvoid::write_console(b"GUI self-check error 88\n");
                libvoid::exit(88);
            }
            if applications.commits(0) != 0 {
                initial_commit = true;
                break;
            }
            libvoid::yield_now();
        }
        if !initial_commit {
            let _ = libvoid::write_console(b"GUI self-check error 89\n");
            libvoid::exit(89);
        }
        if mode == 2 {
            let _ = libvoid::write_console(b"VoidOS gui parent exit probe\n");
            libvoid::exit(42);
        }
        for scalar in b"print gui_ipc_ok\n" {
            if !applications.key(0, u32::from(*scalar), 0) {
                let _ = libvoid::write_console(b"GUI self-check error 90\n");
                libvoid::exit(90);
            }
        }
        let before = applications.commits(0);
        for _ in 0..10000 {
            if !applications.poll(&ui, acq.width, acq.height) {
                let _ = libvoid::write_console(b"GUI self-check error 91\n");
                libvoid::exit(91);
            }
            if applications.commits(0) >= before + 17 {
                break;
            }
            libvoid::yield_now();
        }
        if applications.commits(0) < before + 17 {
            let _ = libvoid::write_console(b"GUI self-check error 92\n");
            libvoid::exit(92);
        }
        slint::platform::update_timers_and_animations();
        let mut presented = false;
        window.draw_if_needed(|renderer| {
            let _dirty = renderer.render(backbuffer, acq.stride as usize);
            // Verify display_present syscall on 1x1 rect without flashing the full desktop before the shell.
            presented = libvoid::display_present(&[libvoid::DisplayRect {
                x: 0,
                y: 0,
                width: 1,
                height: 1,
            }]) == 0;
        });
        if !presented || !applications.shutdown() {
            let _ = libvoid::write_console(b"GUI self-check error 93\n");
            libvoid::exit(93);
        }
        let _ = libvoid::write_console(b"VoidOS gui terminal IPC ok\nVoidOS slint compositor ok\n");
        libvoid::exit(0);
    }

    // Capture real native blurred wallpaper strip behind the dock before showing the dock,
    // and present the full wallpaper once so the entire framebuffer is clean from frame 0.
    ui.set_dock_visible(false);
    window.draw_if_needed(|renderer| {
        let _ = renderer.render(backbuffer, acq.stride as usize);
    });
    let _ = libvoid::display_present(&[libvoid::DisplayRect {
        x: 0,
        y: 0,
        width: acq.width,
        height: acq.height,
    }]);
    let dock_blur = build_dock_blur(backbuffer, acq.stride, acq.width, acq.height);
    let cached_wp = cache_screen_wallpaper(backbuffer, acq.stride, acq.width, acq.height);
    ui.set_cached_wallpaper(cached_wp);
    ui.set_wallpaper_cached(true);
    ui.set_dock_backdrop(dock_blur);
    ui.set_dock_visible(true);

    let _ = libvoid::write_console(b"VoidOS desktop ready\n");

    // Mode 0: live interactive Slint event loop (hardware GPU cursor when bit 31 of format is set, else 20x20 overlay).
    #[derive(Clone, Copy)]
    enum DragOp {
        Move { kind: i32, off_x: f32, off_y: f32 },
        PendingUnmax { kind: i32, start_cx: f32, start_cy: f32 },
        Resize { kind: i32, zone: apps::HitZone, start_cx: f32, start_cy: f32, start_w: f32, start_h: f32 },
    }

    let hw_cursor = (acq.format & 0x8000_0000) != 0;
    let mut cx = (acq.width as f32) * 0.5;
    let mut cy = (acq.height as f32) * 0.5;
    let mut prev_buttons = 0u32;
    let mut dragging: Option<DragOp> = None;
    let mut last_title_click: Option<(i32, u64, f32, f32)> = None;
    let mut last_uptime_second = dinfo.uptime_ms / 1000;
    let mut cursor = cursor::CursorOverlay::new();

    loop {
        if dragging.is_none() {
            if libvoid::display_info(&mut dinfo) != 0 {
                let _ = applications.shutdown();
                libvoid::exit(96);
            }
            if dinfo.width >= 320
                && dinfo.height >= 240
                && (dinfo.width != acq.width || dinfo.height != acq.height)
            {
                let mut next_acq = libvoid::DisplayAcquireOut::default();
                if libvoid::display_acquire(&mut next_acq) == 0
                    && next_acq.user_virt >= 0x1000_0000
                    && next_acq.width > 0
                    && next_acq.height > 0
                {
                    acq = next_acq;
                    total_pixels = (acq.stride as usize).saturating_mul(acq.height as usize);
                    backbuffer = unsafe {
                        core::slice::from_raw_parts_mut(
                            acq.user_virt as *mut BgrxPixel,
                            total_pixels,
                        )
                    };
                    cx = cx.clamp(0.0, (acq.width - 1) as f32);
                    cy = cy.clamp(0.0, (acq.height - 1) as f32);
                    window.set_size(PhysicalSize::new(acq.width, acq.height));
                    ui.set_surfaces_visible(false);
                    ui.set_dock_visible(false);
                    ui.set_wallpaper_cached(false);
                    ui.set_cached_wallpaper(Image::default());
                    ui.set_dock_backdrop(Image::default());
                    window.draw_if_needed(|renderer| {
                        let _ = renderer.render(backbuffer, acq.stride as usize);
                    });
                    let dock_blur = build_dock_blur(backbuffer, acq.stride, acq.width, acq.height);
                    let cached_wp =
                        cache_screen_wallpaper(backbuffer, acq.stride, acq.width, acq.height);
                    ui.set_cached_wallpaper(cached_wp);
                    ui.set_wallpaper_cached(true);
                    ui.set_dock_backdrop(dock_blur);
                    ui.set_dock_visible(true);
                    ui.set_surfaces_visible(true);
                    applications.on_display_resized(
                        &ui,
                        ui.get_dock_mode(),
                        acq.width,
                        acq.height,
                    );
                    window.draw_if_needed(|renderer| {
                        let _ = renderer.render(backbuffer, acq.stride as usize);
                    });
                    let _ = libvoid::display_present(&[libvoid::DisplayRect {
                        x: 0,
                        y: 0,
                        width: acq.width,
                        height: acq.height,
                    }]);
                }
            }
            platform_now.set(dinfo.uptime_ms);
            let second = dinfo.uptime_ms / 1000;
            if second < last_uptime_second {
                let _ = applications.shutdown();
                libvoid::exit(97);
            }
            if second != last_uptime_second {
                let (clock_str, date_str) = format_rtc_labels(&dinfo, acq.width, acq.height);
                ui.set_display_label(clock_str);
                ui.set_uptime_label(date_str);
                last_uptime_second = second;
            }
        }
        slint::platform::update_timers_and_animations();
        let mut ev = libvoid::InputEvent::default();
        let mut mouse_moved = false;
        let mut had_work = false;
        loop {
            let received = libvoid::input_poll(&mut ev);
            if received == 0 {
                break;
            }
            if received != 1 {
                let _ = applications.shutdown();
                libvoid::exit(96);
            }
            had_work = true;
            if ev.kind == libvoid::INPUT_MOUSE {
                let previous_position = (cx, cy);
                if (ev.flags & libvoid::INPUT_FLAG_ABSOLUTE) != 0 {
                    cx = (ev.code as f32).clamp(0.0, (acq.width - 1) as f32);
                    cy = (ev.value as f32).clamp(0.0, (acq.height - 1) as f32);
                } else {
                    cx = (cx + ev.delta_x as f32).clamp(0.0, (acq.width - 1) as f32);
                    cy = (cy - ev.delta_y as f32).clamp(0.0, (acq.height - 1) as f32);
                }
                if (cx, cy) != previous_position {
                    mouse_moved = true;
                }
                let buttons = ev.flags & 0x7;
                let left_now = (buttons & 1) != 0;
                let left_prev = (prev_buttons & 1) != 0;
                if left_now != left_prev {
                    let pos = LogicalPosition::new(cx, cy);
                    if left_now {
                        if let Some(hit) = applications.hit_test(cx, cy) {
                            if applications.focused != hit.kind {
                                applications.focused = hit.kind;
                                applications.refresh(&ui);
                            }
                            match hit.zone {
                                apps::HitZone::Titlebar => {
                                    let is_double = last_title_click.is_some_and(|(lk, lt, lx, ly)| {
                                        lk == hit.kind
                                            && dinfo.uptime_ms.saturating_sub(lt) <= 360
                                            && (cx - lx).abs() <= 8.0
                                            && (cy - ly).abs() <= 8.0
                                    });
                                    if is_double {
                                        last_title_click = None;
                                        dragging = None;
                                        applications.toggle_maximize(
                                            hit.kind,
                                            ui.get_dock_mode(),
                                            acq.width,
                                            acq.height,
                                            dinfo.uptime_ms,
                                        );
                                        applications.refresh(&ui);
                                    } else {
                                        last_title_click = Some((hit.kind, dinfo.uptime_ms, cx, cy));
                                        if hit.maximized {
                                            dragging = Some(DragOp::PendingUnmax {
                                                kind: hit.kind,
                                                start_cx: cx,
                                                start_cy: cy,
                                            });
                                        } else {
                                            dragging = Some(DragOp::Move {
                                                kind: hit.kind,
                                                off_x: cx - hit.pos_x,
                                                off_y: cy - hit.pos_y,
                                            });
                                        }
                                    }
                                    mouse_moved = false;
                                }
                                apps::HitZone::ResizeRight
                                | apps::HitZone::ResizeBottom
                                | apps::HitZone::ResizeBottomRight => {
                                    last_title_click = None;
                                    dragging = Some(DragOp::Resize {
                                        kind: hit.kind,
                                        zone: hit.zone,
                                        start_cx: cx,
                                        start_cy: cy,
                                        start_w: hit.view_w,
                                        start_h: hit.view_h,
                                    });
                                    mouse_moved = false;
                                }
                                apps::HitZone::Client => {
                                    last_title_click = None;
                                    if mouse_moved {
                                        window.dispatch_event(WindowEvent::PointerMoved { position: pos });
                                        mouse_moved = false;
                                    }
                                    window.dispatch_event(WindowEvent::PointerPressed {
                                        position: pos,
                                        button: PointerEventButton::Left,
                                    });
                                }
                            }
                        } else {
                            last_title_click = None;
                            if mouse_moved {
                                window.dispatch_event(WindowEvent::PointerMoved { position: pos });
                                mouse_moved = false;
                            }
                            window.dispatch_event(WindowEvent::PointerPressed {
                                position: pos,
                                button: PointerEventButton::Left,
                            });
                        }
                    } else if let Some(done_op) = dragging.take() {
                        if let DragOp::Resize { kind, .. } = done_op {
                            let _ = libvoid::display_info(&mut dinfo);
                            platform_now.set(dinfo.uptime_ms);
                            applications.finish_resize(kind, dinfo.uptime_ms);
                        }
                        mouse_moved = false;
                    } else {
                        if mouse_moved {
                            window.dispatch_event(WindowEvent::PointerMoved { position: pos });
                            mouse_moved = false;
                        }
                        window.dispatch_event(WindowEvent::PointerReleased {
                            position: pos,
                            button: PointerEventButton::Left,
                        });
                    }
                    prev_buttons = buttons;
                }
            } else if ev.kind == libvoid::INPUT_KEY {
                let scalar = match ev.value {
                    0x110001 => 8,
                    0x110002 => 9,
                    0x110003 => 10,
                    value => value,
                };
                if !applications.key(ev.code, scalar, ev.flags) {
                    ui.set_status("Coda tastiera piena".into());
                }
            }
        }
        if mouse_moved {
            match dragging {
                Some(DragOp::Move { kind, off_x, off_y }) => {
                    had_work = true;
                    applications.move_to(kind, cx - off_x, cy - off_y, acq.width, acq.height);
                }
                Some(DragOp::PendingUnmax { kind, start_cx, start_cy }) => {
                    if (cx - start_cx).abs() > 6.0 || (cy - start_cy).abs() > 6.0 {
                        had_work = true;
                        if let Some((nx, ny)) = applications.unmaximize_for_drag(kind, cx, cy, acq.width) {
                            dragging = Some(DragOp::Move {
                                kind,
                                off_x: cx - nx,
                                off_y: cy - ny,
                            });
                        }
                    }
                }
                Some(DragOp::Resize { kind, zone, start_cx, start_cy, start_w, start_h }) => {
                    had_work = true;
                    let dx = cx - start_cx;
                    let dy = cy - start_cy;
                    let nw = match zone {
                        apps::HitZone::ResizeRight | apps::HitZone::ResizeBottomRight => start_w + dx,
                        _ => start_w,
                    };
                    let nh = match zone {
                        apps::HitZone::ResizeBottom | apps::HitZone::ResizeBottomRight => start_h + dy,
                        _ => start_h,
                    };
                    applications.resize_live(kind, nw, nh, acq.width, acq.height);
                }
                None => {
                    window.dispatch_event(WindowEvent::PointerMoved {
                        position: LogicalPosition::new(cx, cy),
                    });
                }
            }
        }
        while let Some((operation, kind)) = requests.borrow_mut().pop_front() {
            had_work = true;
            let mut need_refresh = true;
            let ok = match operation {
                0 => applications.launch(kind, dinfo.uptime_ms),
                1 => {
                    need_refresh = false;
                    applications.begin_close(kind, dinfo.uptime_ms)
                }
                3 => {
                    applications.begin_minimize(kind, dinfo.uptime_ms);
                    applications.sync_dock_indicators(&ui);
                    need_refresh = false;
                    true
                }
                4 => {
                    applications.toggle_maximize(
                        kind,
                        ui.get_dock_mode(),
                        acq.width,
                        acq.height,
                        dinfo.uptime_ms,
                    );
                    true
                }
                _ => {
                    if applications.focused == kind {
                        need_refresh = false;
                    } else {
                        applications.focused = kind;
                    }
                    true
                }
            };
            if !ok {
                ui.set_status("Applicazione non disponibile".into());
                let _ = libvoid::write_console(if operation == 0 {
                    b"VoidOS ui launch failed\n"
                } else {
                    b"VoidOS ui close failed\n"
                });
            }
            if need_refresh {
                applications.refresh(&ui);
            }
        }
        if !(dragging.is_some() && mouse_moved) {
            let (apps_ok, apps_active) = applications.poll_with_activity(&ui, acq.width, acq.height);
            if apps_active {
                had_work = true;
            }
            if !apps_ok {
                ui.set_status("Errore nel canale dell'applicazione".into());
            }
        }
        if applications.tick_animations(&ui, dinfo.uptime_ms) {
            had_work = true;
        }
        if quitting.get() {
            let ok = applications.shutdown();
            libvoid::exit(if ok { 0 } else { 94 });
        }

        let ix = cx as i32;
        let iy = cy as i32;
        let mut presented = true;
        let mut slint_drew = false;
        window.draw_if_needed(|renderer| {
            slint_drew = true;
            had_work = true;
            if hw_cursor {
                let dirty = renderer.render(backbuffer, acq.stride as usize);
                presented = present_rects(Some(dirty), None, None, acq.width, acq.height);
            } else {
                let old_rect = cursor.restore(backbuffer, acq.stride as usize);
                let dirty = renderer.render(backbuffer, acq.stride as usize);
                let new_rect = cursor.draw(backbuffer, acq.stride as usize, acq.width, acq.height, ix, iy);
                presented = present_rects(Some(dirty), old_rect, new_rect, acq.width, acq.height);
            }
        });
        if !slint_drew && !hw_cursor {
            let (ox, oy, drawn) = cursor.pos();
            if !drawn || ox != ix || oy != iy {
                had_work = true;
                let old_rect = cursor.restore(backbuffer, acq.stride as usize);
                let new_rect = cursor.draw(backbuffer, acq.stride as usize, acq.width, acq.height, ix, iy);
                presented = present_rects(None, old_rect, new_rect, acq.width, acq.height);
            }
        }
        if !presented {
            let _ = applications.shutdown();
            libvoid::exit(95);
        }
        if dragging.is_some() && mouse_moved {
            // Keep dragging loop hot without yielding a full scheduler tick while mouse events arrive.
        } else if had_work || applications.has_pending() || applications.has_active_animations() {
            libvoid::yield_now();
        } else {
            let wait_ms = slint::platform::duration_until_next_timer_update()
                .map(|d| (d.as_millis() as u64).clamp(1, 4))
                .unwrap_or(250);
            let _ = libvoid::gui_wait(wait_ms);
        }
    }
}

// ====== panic ======

struct ConsoleBuf {
    buf: [u8; 256],
    len: usize,
}

impl core::fmt::Write for ConsoleBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let bytes = s.as_bytes();
        let avail = self.buf.len().saturating_sub(self.len);
        let n = bytes.len().min(avail);
        self.buf[self.len..self.len + n].copy_from_slice(&bytes[..n]);
        self.len += n;
        Ok(())
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    use core::fmt::Write;
    let mut out = ConsoleBuf { buf: [0; 256], len: 0 };
    let _ = write!(out, "UI_PANIC: {}\n", info);
    let _ = libvoid::write_console(&out.buf[..out.len]);
    libvoid::exit(99);
}
