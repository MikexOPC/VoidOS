#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::anim::{Easing, Tween};
use libui::draw::{Canvas, Rect};
use libui::proto::{GuiMessage, PROTO_WIRE_BYTES};
use libui::style::{Theme, rgb};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const WIN_W: u32 = 400;
const WIN_H: u32 = 240;
const SHM_PAGES: u64 = ((WIN_W as u64 * WIN_H as u64 * 4) + 4095) / 4096;

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

// ====== draw_meter ======

fn draw_meter(canvas: &mut Canvas<'_>, y: i32, label: &[u8], pct: i32, bar_color: u32, theme: &Theme) {
    canvas.draw_text(16, y, label, theme.text_primary);
    let track = Rect::new(16, y + 20, (WIN_W as i32) - 32, 18);
    canvas.fill_rounded_rect(track, 6, rgb(0x1E, 0x26, 0x36));
    let clamped = pct.clamp(0, 100);
    let fill_w = (track.w * clamped) / 100;
    if fill_w > 0 {
        canvas.fill_rounded_rect(Rect::new(track.x, track.y, fill_w, track.h), 6, bar_color);
    }
    canvas.stroke_rect(track, theme.window_border);
}

// ====== render_sysmon ======

fn render_sysmon(
    canvas: &mut Canvas<'_>,
    theme: &Theme,
    cpu_pct: i32,
    mem_pct: i32,
    dinfo: &libvoid::DisplayInfo,
) {
    canvas.fill_rect(Rect::new(0, 0, WIN_W as i32, WIN_H as i32), theme.window_bg);
    canvas.draw_text(16, 12, b"VoidOS System Monitor", theme.text_primary);
    draw_meter(canvas, 44, b"CPU LOAD", cpu_pct, theme.accent, theme);
    draw_meter(canvas, 96, b"MEMORY", mem_pct, rgb(0x22, 0xC5, 0x5E), theme);

    let mut num = [0u8; 20];
    canvas.draw_text(16, 156, b"DISPLAY:", theme.text_muted);
    let w_txt = libvoid::format_u64(dinfo.width as u64, &mut num);
    canvas.draw_text(96, 156, w_txt, theme.text_primary);
    canvas.draw_text(140, 156, b"X", theme.text_muted);
    let h_txt = libvoid::format_u64(dinfo.height as u64, &mut num);
    canvas.draw_text(156, 156, h_txt, theme.text_primary);

    canvas.draw_text(16, 184, b"UPTIME MS:", theme.text_muted);
    let up_txt = libvoid::format_u64(dinfo.uptime_ms, &mut num);
    canvas.draw_text(108, 184, up_txt, theme.text_primary);
}

// ====== program_main ======

#[unsafe(no_mangle)]
extern "C" fn program_main(arg: u64) -> ! {
    // Isolation invariant: app must never acquire direct display ownership.
    let mut forbidden = libvoid::DisplayAcquireOut::default();
    if libvoid::display_acquire(&mut forbidden) >= 0 {
        libvoid::exit(100);
    }

    let theme = load_theme();
    let shm_id = libvoid::shm_create(SHM_PAGES);
    if shm_id <= 0 {
        libvoid::exit(101);
    }
    let virt = libvoid::shm_map(shm_id as u64);
    if virt < 0x1000_0000 {
        libvoid::exit(102);
    }
    let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (WIN_W * WIN_H) as usize) };
    let mut canvas = Canvas::new(pixels, WIN_W, WIN_H, WIN_W);
    let mut dinfo = libvoid::DisplayInfo::default();
    if libvoid::display_info(&mut dinfo) != 0 {
        libvoid::exit(103);
    }

    let mut cpu_bar = Tween::settled(10);
    let mut mem_bar = Tween::settled(20);

    // Mode 1: self-verification pass (animated resource bars + SHM rendering + clean unmap).
    if arg == 1 {
        cpu_bar.retarget(0, 68, theme.anim_ms, Easing::EaseOutCubic);
        mem_bar.retarget(0, 42, theme.anim_ms, Easing::EaseOutCubic);
        render_sysmon(&mut canvas, &theme, cpu_bar.sample(0), mem_bar.sample(0), &dinfo);
        let px_before = canvas.pixels[(72 * WIN_W as usize) + 180];
        let end_ms = theme.anim_ms as u64;
        render_sysmon(&mut canvas, &theme, cpu_bar.sample(end_ms), mem_bar.sample(end_ms), &dinfo);
        let px_after = canvas.pixels[(72 * WIN_W as usize) + 180];
        if px_before == px_after {
            libvoid::exit(104);
        }
        if libvoid::shm_unmap(shm_id as u64) != 0 {
            libvoid::exit(105);
        }
        let _ = libvoid::write_console(b"VoidOS gui sysmon ok\n");
        libvoid::exit(0);
    }

    // Live IPC window mode: arg carries the IPC channel handle to the compositor.
    let ipc_handle = arg;
    let create = GuiMessage::create_window(WIN_W, WIN_H, shm_id as u64, b"System Monitor");
    let _ = libvoid::ipc_send(ipc_handle, &create.encode());
    let commit = GuiMessage::commit_rect(2, 0, 0, WIN_W, WIN_H).encode();
    let mut rx = [0u8; PROTO_WIRE_BYTES];
    loop {
        let _ = libvoid::display_info(&mut dinfo);
        let target_cpu = 20 + ((dinfo.uptime_ms / 100) % 60) as i32;
        if cpu_bar.is_done(dinfo.uptime_ms) {
            cpu_bar.retarget(dinfo.uptime_ms, target_cpu, theme.anim_ms, Easing::EaseOutCubic);
            mem_bar.retarget(dinfo.uptime_ms, 38, theme.anim_ms, Easing::EaseOutCubic);
        }
        render_sysmon(
            &mut canvas,
            &theme,
            cpu_bar.sample(dinfo.uptime_ms),
            mem_bar.sample(dinfo.uptime_ms),
            &dinfo,
        );
        let _ = libvoid::ipc_send(ipc_handle, &commit);
        let _ = libvoid::ipc_recv(ipc_handle, &mut rx);
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
