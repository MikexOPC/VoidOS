#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
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
const REFRESH_INTERVAL_MS: u64 = 250;

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

// ====== pct_u64 ======

fn pct_u64(used: u64, total: u64) -> i32 {
    if total == 0 {
        return 0;
    }
    ((used.saturating_mul(100)) / total).min(100) as i32
}

// ====== draw_meter ======

fn draw_meter(
    canvas: &mut Canvas<'_>,
    y: i32,
    label: &[u8],
    pct: i32,
    bar_color: u32,
    theme: &Theme,
) {
    let mut num = [0u8; 20];
    canvas.draw_text(16, y, label, theme.text_primary);
    let pct_txt = libvoid::format_u64(pct.clamp(0, 100) as u64, &mut num);
    let right_x = (WIN_W as i32) - 16 - ((pct_txt.len() as i32 + 1) * 8);
    canvas.draw_text(right_x, y, pct_txt, theme.text_primary);
    canvas.draw_text(right_x + (pct_txt.len() as i32 * 8), y, b"%", theme.text_muted);

    let track = Rect::new(16, y + 18, (WIN_W as i32) - 32, 16);
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
    sinfo: &libvoid::SysInfo,
    dinfo: &libvoid::DisplayInfo,
) {
    let phys_used = sinfo.total_phys_pages.saturating_sub(sinfo.free_phys_pages);
    let phys_pct = pct_u64(phys_used, sinfo.total_phys_pages);
    let heap_used = sinfo.heap_total_pages.saturating_sub(sinfo.heap_free_pages);
    let heap_pct = pct_u64(heap_used, sinfo.heap_total_pages);

    canvas.fill_rect(Rect::new(0, 0, WIN_W as i32, WIN_H as i32), theme.window_bg);
    canvas.draw_text(16, 12, b"VoidOS System Monitor", theme.text_primary);

    draw_meter(canvas, 38, b"PHYS RAM", phys_pct, rgb(0x22, 0xC5, 0x5E), theme);
    draw_meter(canvas, 84, b"KERNEL HEAP", heap_pct, theme.accent, theme);

    let mut num = [0u8; 20];
    canvas.draw_text(16, 134, b"RAM KIB:", theme.text_muted);
    let used_kib = phys_used.saturating_mul(4);
    let total_kib = sinfo.total_phys_pages.saturating_mul(4);
    let u_txt = libvoid::format_u64(used_kib, &mut num);
    canvas.draw_text(92, 134, u_txt, theme.text_primary);
    let sep_x = 92 + (u_txt.len() as i32 * 8) + 4;
    canvas.draw_text(sep_x, 134, b"/", theme.text_muted);
    let t_txt = libvoid::format_u64(total_kib, &mut num);
    canvas.draw_text(sep_x + 12, 134, t_txt, theme.text_primary);

    canvas.draw_text(16, 158, b"CPUS:", theme.text_muted);
    let cpu_txt = libvoid::format_u64(sinfo.online_cpus as u64, &mut num);
    canvas.draw_text(64, 158, cpu_txt, theme.text_primary);
    canvas.draw_text(112, 158, b"PROCS:", theme.text_muted);
    let proc_txt = libvoid::format_u64(sinfo.active_processes as u64, &mut num);
    canvas.draw_text(168, 158, proc_txt, theme.text_primary);
    canvas.draw_text(224, 158, b"IPC:", theme.text_muted);
    let ipc_txt = libvoid::format_u64(sinfo.channel_count as u64, &mut num);
    canvas.draw_text(264, 158, ipc_txt, theme.text_primary);

    canvas.draw_text(16, 182, b"DISPLAY:", theme.text_muted);
    let w_txt = libvoid::format_u64(dinfo.width as u64, &mut num);
    canvas.draw_text(92, 182, w_txt, theme.text_primary);
    canvas.draw_text(132, 182, b"x", theme.text_muted);
    let h_txt = libvoid::format_u64(dinfo.height as u64, &mut num);
    canvas.draw_text(146, 182, h_txt, theme.text_primary);

    canvas.draw_text(16, 206, b"UPTIME S:", theme.text_muted);
    let up_txt = libvoid::format_u64(sinfo.uptime_ms / 1000, &mut num);
    canvas.draw_text(96, 206, up_txt, theme.text_primary);
    canvas.draw_text(180, 206, b"TICKS:", theme.text_muted);
    let tick_txt = libvoid::format_u64(sinfo.preempt_ticks, &mut num);
    canvas.draw_text(236, 206, tick_txt, theme.text_primary);
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
    let mut sinfo = libvoid::SysInfo::default();
    if libvoid::sys_info(&mut sinfo) != 0
        || sinfo.total_phys_pages == 0
        || sinfo.online_cpus == 0
        || sinfo.active_processes == 0
    {
        libvoid::exit(106);
    }

    // Mode 1: self-verification pass (real kernel metrics + SHM rendering + clean unmap).
    if arg == 1 {
        let px_before = canvas.pixels[(60 * WIN_W as usize) + 24];
        render_sysmon(&mut canvas, &theme, &sinfo, &dinfo);
        let px_after = canvas.pixels[(60 * WIN_W as usize) + 24];
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
    let ipc_handle = arg & !(1u64 << 63);
    if libvoid::shm_publish(shm_id as u64) != 0 {
        libvoid::exit(107);
    }
    let create = GuiMessage::create_window(WIN_W, WIN_H, shm_id as u64, b"System Monitor");
    send_message(ipc_handle, &create.encode());
    render_sysmon(&mut canvas, &theme, &sinfo, &dinfo);
    let commit = GuiMessage::commit_rect(1, 0, 0, WIN_W, WIN_H).encode();
    send_message(ipc_handle, &commit);

    let mut last_refresh_ms = sinfo.uptime_ms;
    let mut rx = [0u8; PROTO_WIRE_BYTES];
    loop {
        let n = libvoid::ipc_recv(ipc_handle, &mut rx);
        if n == libvoid::ERR_PEER_CLOSED {
            let _ = libvoid::shm_unmap(shm_id as u64);
            libvoid::exit(0);
        }
        if libvoid::sys_info(&mut sinfo) == 0
            && sinfo.uptime_ms.saturating_sub(last_refresh_ms) >= REFRESH_INTERVAL_MS
        {
            last_refresh_ms = sinfo.uptime_ms;
            let _ = libvoid::display_info(&mut dinfo);
            render_sysmon(&mut canvas, &theme, &sinfo, &dinfo);
            send_message(ipc_handle, &commit);
        }
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
