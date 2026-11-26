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
use slint::{LogicalPosition, PhysicalSize};

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

    fn from_rgb(r: u8, g: u8, b: u8) -> Self {
        Self((b as u32) | ((g as u32) << 8) | ((r as u32) << 16) | 0xFF00_0000)
    }
}

// ====== VoidPlatform (Slint Platform backend on VoidOS syscalls) ======

struct VoidPlatform {
    window: Rc<MinimalSoftwareWindow>,
    start_ms: u64,
}

impl Platform for VoidPlatform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
        Ok(self.window.clone())
    }

    fn duration_since_start(&self) -> Duration {
        let mut info = libvoid::DisplayInfo::default();
        if libvoid::display_info(&mut info) == 0 {
            Duration::from_millis(info.uptime_ms.saturating_sub(self.start_ms))
        } else {
            libvoid::exit(96)
        }
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

// ====== format_rtc_labels ======

fn format_rtc_labels(dinfo: &libvoid::DisplayInfo, width: u32, height: u32) -> (slint::SharedString, slint::SharedString) {
    const MONTHS: [&str; 12] = [
        "Gen", "Feb", "Mar", "Apr", "Mag", "Giu",
        "Lug", "Ago", "Set", "Ott", "Nov", "Dic",
    ];
    if let Some((year, month, day, hour, minute, _second)) = dinfo.rtc_date_time() {
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

    if slint::platform::set_platform(Box::new(VoidPlatform {
        window: window.clone(),
        start_ms: dinfo.uptime_ms,
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
    let moves = Rc::new(RefCell::new(VecDeque::<(i32, f32, f32)>::new()));
    let launch = requests.clone();
    ui.on_launch_app(move |kind| launch.borrow_mut().push_back((0, kind)));
    let close = requests.clone();
    ui.on_close_app(move |kind| close.borrow_mut().push_back((1, kind)));
    let focus = requests.clone();
    ui.on_focus_app(move |kind| focus.borrow_mut().push_back((2, kind)));
    let minimize = requests.clone();
    ui.on_minimize_app(move |kind| minimize.borrow_mut().push_back((3, kind)));
    let mv = moves.clone();
    ui.on_move_app(move |kind, dx, dy| mv.borrow_mut().push_back((kind, dx, dy)));
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

    let total_pixels = (acq.stride as usize).saturating_mul(acq.height as usize);
    let backbuffer =
        unsafe { core::slice::from_raw_parts_mut(acq.user_virt as *mut BgrxPixel, total_pixels) };

    if mode == 1 || mode == 2 {
        if !applications.launch(0) {
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

    let _ = libvoid::write_console(b"VoidOS desktop ready\n");

    // Mode 0: live interactive Slint event loop (hardware GPU cursor when bit 31 of format is set, else 20x20 overlay).
    let hw_cursor = (acq.format & 0x8000_0000) != 0;
    let mut cx = (acq.width as f32) * 0.5;
    let mut cy = (acq.height as f32) * 0.5;
    let mut prev_buttons = 0u32;
    let mut last_uptime_second = dinfo.uptime_ms / 1000;
    let mut cursor = cursor::CursorOverlay::new();

    loop {
        if libvoid::display_info(&mut dinfo) != 0 {
            let _ = applications.shutdown();
            libvoid::exit(96);
        }
        {
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
                    if mouse_moved {
                        window.dispatch_event(WindowEvent::PointerMoved { position: pos });
                        mouse_moved = false;
                    }
                    if left_now {
                        window.dispatch_event(WindowEvent::PointerPressed {
                            position: pos,
                            button: PointerEventButton::Left,
                        });
                    } else {
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
            window.dispatch_event(WindowEvent::PointerMoved {
                position: LogicalPosition::new(cx, cy),
            });
        }
        while let Some((kind, dx, dy)) = moves.borrow_mut().pop_front() {
            had_work = true;
            applications.move_by(kind, dx, dy, acq.width, acq.height);
        }
        while let Some((operation, kind)) = requests.borrow_mut().pop_front() {
            had_work = true;
            let mut need_refresh = true;
            let ok = match operation {
                0 => applications.launch(kind),
                1 => applications.close(kind),
                3 => {
                    applications.minimize(kind);
                    if kind == 0 {
                        ui.set_terminal_open(false);
                    } else if kind == 1 {
                        ui.set_sysmon_open(false);
                    } else if kind == 2 {
                        ui.set_files_open(false);
                    }
                    need_refresh = false;
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
        let (apps_ok, apps_active) = applications.poll_with_activity(&ui, acq.width, acq.height);
        if apps_active {
            had_work = true;
        }
        if !apps_ok {
            ui.set_status("Errore nel canale dell'applicazione".into());
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
        if had_work || applications.has_pending() {
            libvoid::yield_now();
        } else {
            let wait_ms = slint::platform::duration_until_next_timer_update()
                .map(|d| (d.as_millis() as u64).clamp(1, 250))
                .unwrap_or(250);
            let _ = libvoid::gui_wait(wait_ms);
        }
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
