#![no_std]
#![no_main]

extern crate alloc;

use alloc::boxed::Box;
use alloc::rc::Rc;
use core::arch::global_asm;
use core::cell::Cell;
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
static ALLOCATOR: libvoid::heap::ShmAllocator = libvoid::heap::ShmAllocator::new(1024); // 4 MiB SHM heap

// ====== BgrxPixel (UEFI GOP / VirtIO-GPU native 32-bit pixel) ======

#[repr(transparent)]
#[derive(Clone, Copy, Default)]
struct BgrxPixel(u32);

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
    fake_ms: Rc<Cell<Option<u64>>>,
}

impl Platform for VoidPlatform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
        Ok(self.window.clone())
    }

    fn duration_since_start(&self) -> Duration {
        if let Some(ms) = self.fake_ms.get() {
            return Duration::from_millis(ms);
        }
        let mut info = libvoid::DisplayInfo::default();
        if libvoid::display_info(&mut info) == 0 {
            Duration::from_millis(info.uptime_ms.saturating_sub(self.start_ms))
        } else {
            Duration::from_millis(0)
        }
    }
}

// ====== present_region ======

fn present_region(
    region: slint::platform::software_renderer::PhysicalRegion,
    max_w: u32,
    max_h: u32,
) -> bool {
    let mut wire = [libvoid::DisplayRect::default(); 16];
    let mut n = 0usize;
    for (origin, size) in region.iter() {
        if n >= 16 {
            break;
        }
        let x0 = origin.x.max(0) as u32;
        let y0 = origin.y.max(0) as u32;
        let w = size.width.min(max_w.saturating_sub(x0));
        let h = size.height.min(max_h.saturating_sub(y0));
        if w > 0 && h > 0 {
            wire[n] = libvoid::DisplayRect {
                x: x0,
                y: y0,
                width: w,
                height: h,
            };
            n += 1;
        }
    }
    if n == 0 {
        return true;
    }
    libvoid::display_present(&wire[..n]) == 0
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
    let _ = libvoid::display_info(&mut dinfo);

    let window = MinimalSoftwareWindow::new(RepaintBufferType::ReusedBuffer);
    window.set_size(PhysicalSize::new(acq.width, acq.height));
    let fake_ms = Rc::new(Cell::new(if mode == 1 { Some(0u64) } else { None }));

    if slint::platform::set_platform(Box::new(VoidPlatform {
        window: window.clone(),
        start_ms: dinfo.uptime_ms,
        fake_ms: fake_ms.clone(),
    }))
    .is_err()
    {
        libvoid::exit(81);
    }

    let ui = match VoidDesktop::new() {
        Ok(u) => u,
        Err(_) => libvoid::exit(82),
    };
    let _ = ui.show();

    let total_pixels = (acq.stride as usize).saturating_mul(acq.height as usize);
    let backbuffer =
        unsafe { core::slice::from_raw_parts_mut(acq.user_virt as *mut BgrxPixel, total_pixels) };

    // Mode 1: self-verification pass (renders Slint desktop + Dock morph animation + clean exit).
    if mode == 1 {
        slint::platform::update_timers_and_animations();
        let mut presented = false;
        window.draw_if_needed(|renderer| {
            let dirty = renderer.render(backbuffer, acq.stride as usize);
            presented = present_region(dirty, acq.width, acq.height);
        });
        if !presented {
            libvoid::exit(83);
        }
        // Verify Slint actually rendered non-zero pixels into the backbuffer.
        if backbuffer[0].0 == 0 || backbuffer[(120 * acq.stride as usize) + 120].0 == 0 {
            libvoid::exit(84);
        }

        // Trigger DefaultDock morph (0 = Floating -> 1 = Full Bar) and advance animation clock.
        ui.set_dock_mode(1);
        fake_ms.set(Some(250));
        slint::platform::update_timers_and_animations();
        window.draw_if_needed(|renderer| {
            let dirty = renderer.render(backbuffer, acq.stride as usize);
            presented = present_region(dirty, acq.width, acq.height);
        });
        if !presented {
            libvoid::exit(85);
        }

        let _ = libvoid::write_console(b"VoidOS slint compositor ok\n");
        libvoid::exit(0);
    }

    // Mode 0: live interactive Slint event loop.
    let mut cx = (acq.width as f32) * 0.5;
    let mut cy = (acq.height as f32) * 0.5;
    let mut prev_buttons = 0u32;
    loop {
        slint::platform::update_timers_and_animations();
        let mut ev = libvoid::InputEvent::default();
        while libvoid::input_poll(&mut ev) == 1 {
            if ev.kind == libvoid::INPUT_MOUSE {
                cx = (cx + ev.delta_x as f32).clamp(0.0, (acq.width - 1) as f32);
                cy = (cy - ev.delta_y as f32).clamp(0.0, (acq.height - 1) as f32);
                ui.set_cursor_x(cx);
                ui.set_cursor_y(cy);
                let pos = LogicalPosition::new(cx, cy);
                window.dispatch_event(WindowEvent::PointerMoved { position: pos });
                let left_now = (ev.flags & 1) != 0;
                let left_prev = (prev_buttons & 1) != 0;
                if left_now && !left_prev {
                    window.dispatch_event(WindowEvent::PointerPressed {
                        position: pos,
                        button: PointerEventButton::Left,
                    });
                } else if !left_now && left_prev {
                    window.dispatch_event(WindowEvent::PointerReleased {
                        position: pos,
                        button: PointerEventButton::Left,
                    });
                }
                prev_buttons = ev.flags;
            } else if ev.kind == libvoid::INPUT_KEY {
                if ev.value == 27 {
                    libvoid::exit(0);
                }
                if ev.value == b'\t' as u32 {
                    ui.set_dock_mode((ui.get_dock_mode() + 1) % 4);
                }
            }
        }
        window.draw_if_needed(|renderer| {
            let dirty = renderer.render(backbuffer, acq.stride as usize);
            let _ = present_region(dirty, acq.width, acq.height);
        });
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
