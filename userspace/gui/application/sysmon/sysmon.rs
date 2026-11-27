#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use libui::draw::{Canvas, Rect};
use libui::proto::{GuiMessage, MSG_RESIZE_WINDOW, PROTO_WIRE_BYTES};
use libui::style::{Theme, rgb};

global_asm!(
    ".section .text._start",
    ".global _start",
    "_start:",
    "call program_main",
    "ud2",
);

const WIN_W: u32 = 400;
const WIN_H: u32 = 264;
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

// ====== pages_for_surface ======

fn pages_for_surface(width: u32, height: u32) -> u64 {
    ((width as u64).saturating_mul(height as u64).saturating_mul(4) + 4095) / 4096
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
    win_w: i32,
    y: i32,
    label: &[u8],
    pct: i32,
    bar_color: u32,
    theme: &Theme,
) {
    let mut num = [0u8; 20];
    canvas.draw_text(16, y, label, theme.text_primary);
    let pct_txt = libvoid::format_u64(pct.clamp(0, 100) as u64, &mut num);
    let right_x = (win_w - 16 - ((pct_txt.len() as i32 + 1) * 8)).max(120);
    canvas.draw_text(right_x, y, pct_txt, theme.text_primary);
    canvas.draw_text(right_x + (pct_txt.len() as i32 * 8), y, b"%", theme.text_muted);

    let track = Rect::new(16, y + 18, (win_w - 32).max(64), 16);
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
    win_w: u32,
    win_h: u32,
    theme: &Theme,
    sinfo: &libvoid::SysInfo,
    dinfo: &libvoid::DisplayInfo,
    ginfo: &libvoid::GpuInfo,
) {
    let w = win_w as i32;
    let h = win_h as i32;
    let phys_used = sinfo.total_phys_pages.saturating_sub(sinfo.free_phys_pages);
    let phys_pct = pct_u64(phys_used, sinfo.total_phys_pages);
    let heap_used = sinfo.heap_total_pages.saturating_sub(sinfo.heap_free_pages);
    let heap_pct = pct_u64(heap_used, sinfo.heap_total_pages);

    canvas.fill_rect(Rect::new(0, 0, w, h), theme.window_bg);
    canvas.draw_text(16, 12, b"VoidOS System Monitor", theme.text_primary);

    draw_meter(canvas, w, 38, b"PHYS RAM", phys_pct, rgb(0x22, 0xC5, 0x5E), theme);
    draw_meter(canvas, w, 84, b"KERNEL HEAP", heap_pct, theme.accent, theme);

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

    canvas.draw_text(16, 230, b"GPU:", theme.text_muted);
    let gpu_color = if ginfo.backend != libvoid::GPU_BACKEND_GOP_SW {
        rgb(0x22, 0xC5, 0x5E)
    } else {
        theme.text_primary
    };
    canvas.draw_text(56, 230, ginfo.name_str().as_bytes(), gpu_color);
    canvas.draw_text(210, 230, b"PRES:", theme.text_muted);
    let pres_txt = libvoid::format_u64(ginfo.present_calls, &mut num);
    canvas.draw_text(258, 230, pres_txt, theme.text_primary);
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
        libvoid::exit(100);
    }

    let theme = load_theme();
    let mut cur_w = WIN_W;
    let mut cur_h = WIN_H;
    let mut shm_cap_pages = pages_for_surface(cur_w, cur_h);
    let created = libvoid::shm_create(shm_cap_pages);
    if created <= 0 {
        libvoid::exit(101);
    }
    let mut shm_id = created as u64;
    let mapped = libvoid::shm_map(shm_id);
    if mapped < 0x1000_0000 {
        libvoid::exit(102);
    }
    let mut virt = mapped as u64;
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
    let mut ginfo = libvoid::GpuInfo::default();
    if libvoid::gpu_info(&mut ginfo) != 0 {
        libvoid::exit(108);
    }

    if arg == 1 {
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        let px_before = canvas.pixels[(60 * cur_w as usize) + 24];
        render_sysmon(&mut canvas, cur_w, cur_h, &theme, &sinfo, &dinfo, &ginfo);
        let px_after = canvas.pixels[(60 * cur_w as usize) + 24];
        if px_before == px_after {
            libvoid::exit(104);
        }
        if libvoid::shm_unmap(shm_id) != 0 {
            libvoid::exit(105);
        }
        let _ = libvoid::write_console(b"VoidOS gui sysmon ok\n");
        libvoid::exit(0);
    }

    let ipc_handle = arg & !(1u64 << 63);
    if libvoid::shm_publish(shm_id) != 0 {
        libvoid::exit(107);
    }
    let create = GuiMessage::create_window(cur_w, cur_h, shm_id, b"System Monitor");
    send_message(ipc_handle, &create.encode());
    {
        let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
        let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
        render_sysmon(&mut canvas, cur_w, cur_h, &theme, &sinfo, &dinfo, &ginfo);
    }
    send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());

    let mut last_refresh_ms = sinfo.uptime_ms;
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
                            let _ = libvoid::sys_info(&mut sinfo);
                            let _ = libvoid::display_info(&mut dinfo);
                            let _ = libvoid::gpu_info(&mut ginfo);
                            let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
                            let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
                            render_sysmon(&mut canvas, cur_w, cur_h, &theme, &sinfo, &dinfo, &ginfo);
                            send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());
                        }
                    }
                }
            }
        }
        if libvoid::sys_info(&mut sinfo) == 0
            && sinfo.uptime_ms.saturating_sub(last_refresh_ms) >= REFRESH_INTERVAL_MS
        {
            last_refresh_ms = sinfo.uptime_ms;
            let _ = libvoid::display_info(&mut dinfo);
            let _ = libvoid::gpu_info(&mut ginfo);
            let pixels = unsafe { core::slice::from_raw_parts_mut(virt as *mut u32, (cur_w * cur_h) as usize) };
            let mut canvas = Canvas::new(pixels, cur_w, cur_h, cur_w);
            render_sysmon(&mut canvas, cur_w, cur_h, &theme, &sinfo, &dinfo, &ginfo);
            send_message(ipc_handle, &GuiMessage::commit_rect(1, 0, 0, cur_w, cur_h).encode());
        }
        libvoid::yield_now();
    }
}

// ====== panic ======

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    libvoid::exit(99);
}
