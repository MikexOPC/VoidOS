use crate::{AppSurface, VoidDesktop};
use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::vec::Vec;
use libui::proto::{
    GuiMessage, MAX_TITLE_BYTES, MSG_CLIPBOARD_GET, MSG_CLIPBOARD_SET, MSG_COMMIT_RECT,
    MSG_CREATE_WINDOW, MSG_RESIZE_WINDOW, PROTO_WIRE_BYTES,
};
use slint::{Image, Model, Rgb8Pixel, SharedPixelBuffer, VecModel};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum HitZone {
    Titlebar,
    Client,
    ResizeRight,
    ResizeBottom,
    ResizeBottomRight,
}

#[derive(Clone, Copy)]
pub struct HitResult {
    pub kind: i32,
    pub zone: HitZone,
    pub pos_x: f32,
    pub pos_y: f32,
    pub view_w: f32,
    pub view_h: f32,
    pub maximized: bool,
}

#[derive(Clone, Copy)]
pub struct MaximizedLayout {
    pub x: f32,
    pub y: f32,
    pub client_w: u32,
    pub client_h: u32,
    pub top_radius: f32,
    pub bottom_radius: f32,
}

// ====== dock_maximized_layout ======

// Per-dock maximize geometry policy:
// - dock_mode == 0 (Default Dock): reserves the bottom floating dock strip with an 8px breathing gap
//   and rounds ONLY the bottom corners (14px) while keeping the top corners flush (0px).
// - dock_mode != 0 (future docks): neutral full-screen geometry without bottom corner radius until
//   dedicated dock layouts are added natively.
pub fn dock_maximized_layout(dock_mode: i32, display_w: u32, display_h: u32) -> MaximizedLayout {
    if dock_mode == 0 {
        let win_h = display_h.saturating_sub(70).max(200);
        MaximizedLayout {
            x: 0.0,
            y: 0.0,
            client_w: display_w.saturating_sub(2).max(240),
            client_h: win_h.saturating_sub(34).max(160),
            top_radius: 0.0,
            bottom_radius: 14.0,
        }
    } else {
        MaximizedLayout {
            x: 0.0,
            y: 0.0,
            client_w: display_w.saturating_sub(2).max(240),
            client_h: display_h.saturating_sub(34).max(160),
            top_radius: 0.0,
            bottom_radius: 0.0,
        }
    }
}

pub struct Application {
    kind: i32,
    child: libvoid::ConnectedChild,
    shm: u64,
    view: libvoid::ShmView,
    width: u32,
    height: u32,
    view_w: f32,
    view_h: f32,
    pos_x: f32,
    pos_y: f32,
    saved_x: f32,
    saved_y: f32,
    saved_w: f32,
    saved_h: f32,
    minimized: bool,
    maximized: bool,
    top_radius: f32,
    bottom_radius: f32,
    // anim_mode: 0 = idle, 1 = open, 2 = close, 3 = minimize, 4 = geometry/maximize/resize
    anim_mode: u8,
    anim_start_ms: u64,
    scale_progress: f32,
    geo_from_x: f32,
    geo_from_y: f32,
    geo_from_w: f32,
    geo_from_h: f32,
    geo_from_tr: f32,
    geo_from_br: f32,
    geo_to_x: f32,
    geo_to_y: f32,
    geo_to_w: f32,
    geo_to_h: f32,
    geo_to_tr: f32,
    geo_to_br: f32,
    geo_anim_ms: f32,
    buffer: SharedPixelBuffer<Rgb8Pixel>,
    image: Image,
    pending: VecDeque<[u8; PROTO_WIRE_BYTES]>,
    pub commits: u64,
    probe_read_only: bool,
}

pub struct Applications {
    windows: Vec<Application>,
    model: Rc<VecModel<AppSurface>>,
    rr_cursor: usize,
    pub focused: i32,
    verify: bool,
    clipboard: [u8; MAX_TITLE_BYTES],
    clipboard_len: usize,
}

impl Applications {
    // ====== new ======

    pub fn new(verify: bool) -> Self {
        Self {
            windows: Vec::new(),
            model: Rc::new(VecModel::default()),
            rr_cursor: 0,
            focused: 0,
            verify,
            clipboard: [0u8; MAX_TITLE_BYTES],
            clipboard_len: 0,
        }
    }

    // ====== model ======

    pub fn model(&self) -> Rc<VecModel<AppSurface>> {
        self.model.clone()
    }

    // ====== send_scroll ======

    pub fn send_scroll(&mut self, x: f32, y: f32, delta_y: i32) -> bool {
        let target_kind = self.hit_test(x, y).map(|h| h.kind).unwrap_or(self.focused);
        for app in &mut self.windows {
            if app.kind == target_kind && !app.minimized {
                if app.pending.len() < 4096 {
                    app.pending.push_back(
                        GuiMessage::input_scroll(1, x as i32, y as i32, delta_y).encode(),
                    );
                }
                return true;
            }
        }
        false
    }

    // ====== launch ======

    pub fn launch(&mut self, kind: i32, now_ms: u64) -> bool {
        let path: &[u8] = match kind {
            0 => b"/bin/terminal.elf",
            1 => b"/bin/sysmon.elf",
            2 => b"/bin/files.elf",
            _ => return false,
        };
        self.focused = kind;
        if let Some(app) = self.windows.iter_mut().find(|app| app.kind == kind) {
            app.minimized = false;
            app.anim_mode = if self.verify { 0 } else { 1 };
            app.anim_start_ms = now_ms;
            app.scale_progress = if self.verify { 1.0 } else { 0.05 };
            return true;
        }
        let budget = libvoid::SpawnBudget {
            max_image_pages: 128,
            max_open_files: 8,
            cpu_budget_ms: 2000,
            flags: 0,
        };
        let mut child = libvoid::ConnectedChild::default();
        if libvoid::spawn_connected(path, budget, &mut child) <= 0 {
            return false;
        }
        let (init_x, init_y) = match kind {
            0 => (96.0, 84.0),
            1 => (810.0, 84.0),
            _ => (220.0, 120.0),
        };
        self.windows.push(Application {
            kind,
            child,
            shm: 0,
            view: libvoid::ShmView::default(),
            width: 0,
            height: 0,
            view_w: 0.0,
            view_h: 0.0,
            pos_x: init_x,
            pos_y: init_y,
            saved_x: init_x,
            saved_y: init_y,
            saved_w: 0.0,
            saved_h: 0.0,
            minimized: false,
            maximized: false,
            top_radius: 12.0,
            bottom_radius: 12.0,
            anim_mode: if self.verify { 0 } else { 1 },
            anim_start_ms: now_ms,
            scale_progress: if self.verify { 1.0 } else { 0.05 },
            geo_from_x: init_x,
            geo_from_y: init_y,
            geo_from_w: 0.0,
            geo_from_h: 0.0,
            geo_from_tr: 12.0,
            geo_from_br: 12.0,
            geo_to_x: init_x,
            geo_to_y: init_y,
            geo_to_w: 0.0,
            geo_to_h: 0.0,
            geo_to_tr: 12.0,
            geo_to_br: 12.0,
            geo_anim_ms: 240.0,
            buffer: SharedPixelBuffer::new(0, 0),
            image: Image::default(),
            pending: VecDeque::new(),
            commits: 0,
            probe_read_only: self.verify,
        });
        true
    }

    // ====== begin_close ======

    pub fn begin_close(&mut self, kind: i32, now_ms: u64) -> bool {
        if self.verify {
            return self.close(kind);
        }
        if let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) {
            if app.commits == 0 || app.minimized {
                return self.close(kind);
            }
            app.anim_mode = 2;
            app.anim_start_ms = now_ms;
            return true;
        }
        true
    }

    // ====== begin_minimize ======

    pub fn begin_minimize(&mut self, kind: i32, now_ms: u64) {
        if self.verify {
            self.minimize(kind);
            return;
        }
        if let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) {
            if app.commits == 0 || app.minimized {
                app.minimized = true;
                return;
            }
            app.anim_mode = 3;
            app.anim_start_ms = now_ms;
        }
    }

    // ====== toggle_maximize ======

    pub fn toggle_maximize(
        &mut self,
        kind: i32,
        dock_mode: i32,
        display_w: u32,
        display_h: u32,
        now_ms: u64,
    ) {
        let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) else {
            return;
        };
        if app.commits == 0 || app.minimized {
            return;
        }
        self.focused = kind;
        let (to_x, to_y, to_w, to_h, to_tr, to_br, dur) = if !app.maximized {
            app.saved_x = app.pos_x;
            app.saved_y = app.pos_y;
            app.saved_w = app.view_w.max(240.0);
            app.saved_h = app.view_h.max(160.0);
            app.maximized = true;
            let layout = dock_maximized_layout(dock_mode, display_w, display_h);
            (
                layout.x,
                layout.y,
                layout.client_w as f32,
                layout.client_h as f32,
                layout.top_radius,
                layout.bottom_radius,
                240.0,
            )
        } else {
            app.maximized = false;
            (
                app.saved_x,
                app.saved_y,
                app.saved_w.max(240.0),
                app.saved_h.max(160.0),
                12.0,
                12.0,
                220.0,
            )
        };

        app.geo_from_x = app.pos_x;
        app.geo_from_y = app.pos_y;
        app.geo_from_w = app.view_w;
        app.geo_from_h = app.view_h;
        app.geo_from_tr = app.top_radius;
        app.geo_from_br = app.bottom_radius;
        app.geo_to_x = to_x;
        app.geo_to_y = to_y;
        app.geo_to_w = to_w;
        app.geo_to_h = to_h;
        app.geo_to_tr = to_tr;
        app.geo_to_br = to_br;
        app.geo_anim_ms = dur;
        app.scale_progress = 1.0;

        if self.verify {
            app.pos_x = to_x;
            app.pos_y = to_y;
            app.view_w = to_w;
            app.view_h = to_h;
            app.top_radius = to_tr;
            app.bottom_radius = to_br;
            app.anim_mode = 0;
        } else {
            app.anim_mode = 4;
            app.anim_start_ms = now_ms;
        }

        if app.pending.len() < 4096 {
            app.pending.push_back(
                GuiMessage::resize_window(1, to_w as u32, to_h as u32, 0).encode(),
            );
        }
    }

    // ====== unmaximize_for_drag ======

    pub fn unmaximize_for_drag(
        &mut self,
        kind: i32,
        cursor_x: f32,
        cursor_y: f32,
        display_w: u32,
    ) -> Option<(f32, f32)> {
        let app = self.windows.iter_mut().find(|a| a.kind == kind)?;
        if !app.maximized {
            return Some((app.pos_x, app.pos_y));
        }
        let restore_w = app.saved_w.max(320.0);
        let restore_h = app.saved_h.max(200.0);
        let frac = (cursor_x / (display_w.max(1) as f32)).clamp(0.1, 0.85);
        let new_x = (cursor_x - frac * (restore_w + 2.0)).clamp(0.0, (display_w as f32 - 80.0).max(0.0));
        let new_y = (cursor_y - 16.0).max(0.0);
        app.maximized = false;
        app.anim_mode = 0;
        app.pos_x = new_x;
        app.pos_y = new_y;
        app.view_w = restore_w;
        app.view_h = restore_h;
        app.top_radius = 12.0;
        app.bottom_radius = 12.0;
        if app.pending.len() < 4096 {
            app.pending.push_back(
                GuiMessage::resize_window(1, restore_w as u32, restore_h as u32, 0).encode(),
            );
        }
        for row in 0..self.model.row_count() {
            if let Some(mut surface) = self.model.row_data(row) {
                if surface.kind == kind {
                    surface.pos_x = new_x;
                    surface.pos_y = new_y;
                    surface.view_width = restore_w;
                    surface.view_height = restore_h;
                    surface.maximized = false;
                    surface.corner_top_radius = 12.0;
                    surface.corner_bottom_radius = 12.0;
                    self.model.set_row_data(row, surface);
                    break;
                }
            }
        }
        Some((new_x, new_y))
    }

    // ====== resize_live ======

    pub fn resize_live(&mut self, kind: i32, new_w: f32, new_h: f32, display_w: u32, display_h: u32) {
        let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) else {
            return;
        };
        if app.maximized {
            return;
        }
        let max_w = ((display_w as f32) - app.pos_x - 2.0).max(320.0);
        let max_h = ((display_h as f32) - app.pos_y - 34.0).max(200.0);
        let cw = new_w.clamp(320.0, max_w);
        let ch = new_h.clamp(200.0, max_h);
        if (app.view_w - cw).abs() < 0.5 && (app.view_h - ch).abs() < 0.5 {
            return;
        }
        app.anim_mode = 0;
        app.view_w = cw;
        app.view_h = ch;
        app.saved_w = cw;
        app.saved_h = ch;
        for row in 0..self.model.row_count() {
            if let Some(mut surface) = self.model.row_data(row) {
                if surface.kind == kind {
                    surface.view_width = cw;
                    surface.view_height = ch;
                    self.model.set_row_data(row, surface);
                    break;
                }
            }
        }
    }

    // ====== finish_resize ======

    pub fn finish_resize(&mut self, kind: i32, now_ms: u64) {
        let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) else {
            return;
        };
        if app.commits == 0 || app.maximized {
            return;
        }
        let target_w = ((app.view_w as u32) & !1).max(320);
        let target_h = ((app.view_h as u32) & !1).max(200);
        app.geo_from_x = app.pos_x;
        app.geo_from_y = app.pos_y;
        app.geo_from_w = app.view_w;
        app.geo_from_h = app.view_h;
        app.geo_from_tr = app.top_radius;
        app.geo_from_br = app.bottom_radius;
        app.geo_to_x = app.pos_x;
        app.geo_to_y = app.pos_y;
        app.geo_to_w = target_w as f32;
        app.geo_to_h = target_h as f32;
        app.geo_to_tr = 12.0;
        app.geo_to_br = 12.0;
        app.geo_anim_ms = 110.0;
        app.anim_mode = 4;
        app.anim_start_ms = now_ms;
        if app.pending.len() < 4096 {
            app.pending
                .push_back(GuiMessage::resize_window(1, target_w, target_h, 0).encode());
        }
    }

    // ====== has_active_animations ======

    pub fn has_active_animations(&self) -> bool {
        self.windows.iter().any(|app| app.anim_mode != 0 && app.commits > 0 && !app.minimized)
    }

    // ====== tick_animations ======

    pub fn tick_animations(&mut self, ui: &VoidDesktop, now_ms: u64) -> bool {
        const OPEN_MS: f32 = 220.0;
        const CLOSE_MS: f32 = 170.0;
        const MIN_MS: f32 = 190.0;
        let mut to_close = Vec::new();
        let mut any_updated = false;
        let mut any_minimized = false;
        for app in &mut self.windows {
            if app.anim_mode == 0 || app.commits == 0 || app.minimized {
                continue;
            }
            let elapsed = now_ms.saturating_sub(app.anim_start_ms) as f32;
            let mut finished_min = false;
            let cur_anim_kind = match app.anim_mode {
                2 => 1,
                3 => 2,
                _ => 0,
            };
            if app.anim_mode == 1 {
                let t = (elapsed / OPEN_MS).clamp(0.0, 1.0);
                let inv = 1.0 - t;
                let inv2 = inv * inv;
                app.scale_progress = 1.0 - inv2 * inv2;
                if t >= 1.0 {
                    app.scale_progress = 1.0;
                    app.anim_mode = 0;
                }
                any_updated = true;
            } else if app.anim_mode == 2 || app.anim_mode == 3 {
                let dur = if app.anim_mode == 2 { CLOSE_MS } else { MIN_MS };
                let t = (elapsed / dur).clamp(0.0, 1.0);
                let eased = if t < 0.5 {
                    4.0 * t * t * t
                } else {
                    let f = -2.0 * t + 2.0;
                    1.0 - (f * f * f) * 0.5
                };
                app.scale_progress = (1.0 - eased).max(0.0);
                if t >= 1.0 {
                    let was_close = app.anim_mode == 2;
                    app.scale_progress = 0.0;
                    app.anim_mode = 0;
                    if was_close {
                        to_close.push(app.kind);
                    } else {
                        app.minimized = true;
                        finished_min = true;
                        any_minimized = true;
                    }
                }
                any_updated = true;
            } else if app.anim_mode == 4 {
                let t = (elapsed / app.geo_anim_ms.max(1.0)).clamp(0.0, 1.0);
                // Smooth ease-out quintic for natural maximize/restore/resize glide
                let inv = 1.0 - t;
                let inv2 = inv * inv;
                let e = 1.0 - inv2 * inv2 * inv;
                app.pos_x = app.geo_from_x + (app.geo_to_x - app.geo_from_x) * e;
                app.pos_y = app.geo_from_y + (app.geo_to_y - app.geo_from_y) * e;
                app.view_w = app.geo_from_w + (app.geo_to_w - app.geo_from_w) * e;
                app.view_h = app.geo_from_h + (app.geo_to_h - app.geo_from_h) * e;
                app.top_radius = app.geo_from_tr + (app.geo_to_tr - app.geo_from_tr) * e;
                app.bottom_radius = app.geo_from_br + (app.geo_to_br - app.geo_from_br) * e;
                if t >= 1.0 {
                    app.pos_x = app.geo_to_x;
                    app.pos_y = app.geo_to_y;
                    app.view_w = app.geo_to_w;
                    app.view_h = app.geo_to_h;
                    app.top_radius = app.geo_to_tr;
                    app.bottom_radius = app.geo_to_br;
                    app.anim_mode = 0;
                }
                any_updated = true;
            }
            let kind = app.kind;
            let progress = app.scale_progress;
            let pos_x = app.pos_x;
            let pos_y = app.pos_y;
            let view_w = app.view_w;
            let view_h = app.view_h;
            let top_r = app.top_radius;
            let bot_r = app.bottom_radius;
            let is_max = app.maximized;
            for row in 0..self.model.row_count() {
                if let Some(mut surface) = self.model.row_data(row) {
                    if surface.kind == kind {
                        surface.scale_progress = progress;
                        surface.anim_kind = cur_anim_kind;
                        surface.pos_x = pos_x;
                        surface.pos_y = pos_y;
                        surface.view_width = view_w;
                        surface.view_height = view_h;
                        surface.corner_top_radius = top_r;
                        surface.corner_bottom_radius = bot_r;
                        surface.maximized = is_max;
                        if finished_min {
                            surface.minimized = true;
                        }
                        self.model.set_row_data(row, surface);
                        break;
                    }
                }
            }
        }
        if !to_close.is_empty() {
            for kind in to_close {
                let _ = self.close(kind);
            }
            self.refresh(ui);
        } else if any_minimized {
            self.sync_dock_indicators(ui);
        }
        any_updated
    }

    // ====== hit_test ======

    pub fn hit_test(&self, x: f32, y: f32) -> Option<HitResult> {
        let ordered = self
            .windows
            .iter()
            .filter(|app| app.kind == self.focused)
            .chain(self.windows.iter().rev().filter(|app| app.kind != self.focused));
        for app in ordered {
            if app.minimized || app.commits == 0 || app.anim_mode == 2 || app.anim_mode == 3 {
                continue;
            }
            let total_w = app.view_w + 2.0;
            let total_h = app.view_h + 34.0;
            if x >= app.pos_x
                && x < app.pos_x + total_w
                && y >= app.pos_y
                && y < app.pos_y + total_h
            {
                let zone = if y < app.pos_y + 32.0 && x < app.pos_x + (total_w - 88.0).max(0.0) {
                    HitZone::Titlebar
                } else if !app.maximized
                    && x >= app.pos_x + total_w - 16.0
                    && y >= app.pos_y + total_h - 16.0
                {
                    HitZone::ResizeBottomRight
                } else if !app.maximized
                    && x >= app.pos_x + total_w - 8.0
                    && y >= app.pos_y + 32.0
                {
                    HitZone::ResizeRight
                } else if !app.maximized && y >= app.pos_y + total_h - 8.0 {
                    HitZone::ResizeBottom
                } else {
                    HitZone::Client
                };
                return Some(HitResult {
                    kind: app.kind,
                    zone,
                    pos_x: app.pos_x,
                    pos_y: app.pos_y,
                    view_w: app.view_w,
                    view_h: app.view_h,
                    maximized: app.maximized,
                });
            }
        }
        None
    }

    // ====== on_display_resized ======

    pub fn on_display_resized(
        &mut self,
        ui: &VoidDesktop,
        dock_mode: i32,
        display_w: u32,
        display_h: u32,
    ) {
        let max_x = (display_w as f32 - 80.0).max(0.0);
        let max_y = (display_h as f32 - 64.0).max(0.0);
        for app in &mut self.windows {
            if app.commits == 0 {
                continue;
            }
            if app.maximized {
                let layout = dock_maximized_layout(dock_mode, display_w, display_h);
                app.pos_x = layout.x;
                app.pos_y = layout.y;
                app.view_w = layout.client_w as f32;
                app.view_h = layout.client_h as f32;
                app.top_radius = layout.top_radius;
                app.bottom_radius = layout.bottom_radius;
                app.anim_mode = 0;
                if app.pending.len() < 4096 {
                    app.pending.push_back(
                        GuiMessage::resize_window(1, layout.client_w, layout.client_h, 0).encode(),
                    );
                }
            } else {
                app.pos_x = app.pos_x.clamp(0.0, max_x);
                app.pos_y = app.pos_y.clamp(0.0, max_y);
                app.saved_x = app.pos_x;
                app.saved_y = app.pos_y;
            }
        }
        self.refresh(ui);
    }

    // ====== move_to ======

    pub fn move_to(&mut self, kind: i32, nx: f32, ny: f32, display_w: u32, display_h: u32) {
        let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) else {
            return;
        };
        let max_x = (display_w as f32 - 80.0).max(0.0);
        let max_y = (display_h as f32 - 64.0).max(0.0);
        let clamped_x = nx.clamp(0.0, max_x);
        let clamped_y = ny.clamp(0.0, max_y);
        if (app.pos_x - clamped_x).abs() < 0.5 && (app.pos_y - clamped_y).abs() < 0.5 {
            return;
        }
        app.pos_x = clamped_x;
        app.pos_y = clamped_y;
        app.saved_x = clamped_x;
        app.saved_y = clamped_y;
        for row in 0..self.model.row_count() {
            if let Some(mut surface) = self.model.row_data(row) {
                if surface.kind == kind {
                    surface.pos_x = clamped_x;
                    surface.pos_y = clamped_y;
                    self.model.set_row_data(row, surface);
                    break;
                }
            }
        }
    }

    // ====== minimize ======

    pub fn minimize(&mut self, kind: i32) {
        if let Some(app) = self.windows.iter_mut().find(|a| a.kind == kind) {
            app.minimized = true;
            for row in 0..self.model.row_count() {
                if let Some(mut surface) = self.model.row_data(row) {
                    if surface.kind == kind {
                        surface.minimized = true;
                        self.model.set_row_data(row, surface);
                        break;
                    }
                }
            }
        }
    }

    // ====== close ======

    pub fn close(&mut self, kind: i32) -> bool {
        let Some(index) = self.windows.iter().position(|app| app.kind == kind) else {
            return true;
        };
        let app = &self.windows[index];
        if libvoid::process_stop(app.child.pid) != 0
            || libvoid::try_wait_pid(app.child.pid, None) != 0
        {
            return false;
        }
        let app = self.windows.remove(index);
        let closed = libvoid::ipc_close(app.child.endpoint) == 0;
        let unmapped = app.shm == 0 || libvoid::shm_unmap(app.shm) == 0;
        closed && unmapped
    }

    // ====== shutdown ======

    pub fn shutdown(&mut self) -> bool {
        while let Some(app) = self.windows.last() {
            if !self.close(app.kind) {
                return false;
            }
        }
        true
    }

    // ====== key ======

    pub fn key(&mut self, code: u32, scalar: u32, flags: u32) -> bool {
        let Some(app) = self.windows.iter_mut().find(|app| app.kind == self.focused) else {
            return true;
        };
        if app.pending.len() >= 4096 {
            return false;
        }
        app.pending
            .push_back(GuiMessage::input_key(1, code, scalar, flags).encode());
        true
    }

    // ====== poll ======

    pub fn poll(&mut self, ui: &VoidDesktop, display_width: u32, display_height: u32) -> bool {
        self.poll_with_activity(ui, display_width, display_height).0
    }

    // ====== poll_with_activity ======

    pub fn poll_with_activity(
        &mut self,
        ui: &VoidDesktop,
        display_width: u32,
        display_height: u32,
    ) -> (bool, bool) {
        const MAX_MSGS_PER_APP: usize = 16;
        let mut structure_changed = false;
        let mut activity = false;
        let mut healthy = true;
        let mut dead = Vec::new();
        let count = self.windows.len();
        if count > 0 {
            let start = self.rr_cursor % count;
            self.rr_cursor = (start + 1) % count;
            for step in 0..count {
                let idx = (start + step) % count;
                let app = &mut self.windows[idx];
                let mut skip_recv = false;
                if app.probe_read_only && app.shm != 0 {
                    let result = libvoid::raw_syscall(
                        libvoid::SYS_RECV,
                        app.child.endpoint,
                        app.view.address,
                        PROTO_WIRE_BYTES as u64,
                    );
                    if result == libvoid::ERR_WOULD_BLOCK {
                        skip_recv = true;
                    } else if result != -2 {
                        healthy = false;
                        dead.push(app.kind);
                        continue;
                    } else {
                        app.probe_read_only = false;
                        activity = true;
                        let _ = libvoid::write_console(b"VoidOS gui surface read-only ok\n");
                    }
                }
                let mut app_dead = false;
                let mut pixels_updated = false;
                let prev_commits = app.commits;
                if !skip_recv {
                    for _ in 0..MAX_MSGS_PER_APP {
                        let mut wire = [0; PROTO_WIRE_BYTES];
                        let result = libvoid::ipc_recv(app.child.endpoint, &mut wire);
                        if result == PROTO_WIRE_BYTES as i64 {
                            let valid = GuiMessage::decode(&wire).is_some_and(|message| {
                                if message.kind == MSG_CLIPBOARD_SET {
                                    let n = (message.e as usize).min(MAX_TITLE_BYTES);
                                    self.clipboard[..n].copy_from_slice(&message.title[..n]);
                                    self.clipboard_len = n;
                                    return true;
                                }
                                if message.kind == MSG_CLIPBOARD_GET {
                                    let reply = GuiMessage::clipboard_data(
                                        1,
                                        &self.clipboard[..self.clipboard_len],
                                    )
                                    .encode();
                                    app.pending.push_back(reply);
                                    return true;
                                }
                                if message.kind == MSG_COMMIT_RECT || message.kind == MSG_RESIZE_WINDOW {
                                    for row in 0..self.model.row_count() {
                                        if let Some(mut surface) = self.model.row_data(row) {
                                            if surface.kind == app.kind {
                                                surface.pixels = Image::default();
                                                self.model.set_row_data(row, surface);
                                                break;
                                            }
                                        }
                                    }
                                    if message.kind == MSG_COMMIT_RECT {
                                        pixels_updated = true;
                                    }
                                }
                                app.receive(message, display_width, display_height)
                            });
                            if !valid {
                                healthy = false;
                                dead.push(app.kind);
                                app_dead = true;
                                break;
                            }
                            activity = true;
                            if app.probe_read_only && app.shm != 0 {
                                break;
                            }
                        } else if result == libvoid::ERR_PEER_CLOSED {
                            dead.push(app.kind);
                            app_dead = true;
                            break;
                        } else if result == libvoid::ERR_WOULD_BLOCK {
                            break;
                        } else {
                            healthy = false;
                            dead.push(app.kind);
                            app_dead = true;
                            break;
                        }
                    }
                }
                if app_dead {
                    continue;
                }
                if pixels_updated {
                    if prev_commits == 0 {
                        structure_changed = true;
                    } else {
                        let mut found = false;
                        for row in 0..self.model.row_count() {
                            if let Some(mut surface) = self.model.row_data(row) {
                                if surface.kind == app.kind {
                                    surface.pixels = app.image.clone();
                                    surface.client_width = app.width as f32;
                                    surface.client_height = app.height as f32;
                                    self.model.set_row_data(row, surface);
                                    found = true;
                                    break;
                                }
                            }
                        }
                        if !found {
                            structure_changed = true;
                        }
                    }
                }
                for _ in 0..MAX_MSGS_PER_APP {
                    let Some(wire) = app.pending.front() else {
                        break;
                    };
                    let result = libvoid::ipc_send(app.child.endpoint, wire);
                    if result == PROTO_WIRE_BYTES as i64 {
                        app.pending.pop_front();
                        activity = true;
                    } else if result == libvoid::ERR_WOULD_BLOCK {
                        break;
                    } else {
                        healthy = false;
                        dead.push(app.kind);
                        break;
                    }
                }
            }
        }
        for kind in dead {
            if !self.close(kind) {
                return (false, true);
            }
            structure_changed = true;
            activity = true;
        }
        if structure_changed {
            self.refresh(ui);
        }
        (healthy, activity)
    }

    // ====== sync_dock_indicators ======

    pub fn sync_dock_indicators(&self, ui: &VoidDesktop) {
        ui.set_terminal_open(self.windows.iter().any(|app| app.kind == 0 && !app.minimized && app.anim_mode != 3));
        ui.set_sysmon_open(self.windows.iter().any(|app| app.kind == 1 && !app.minimized && app.anim_mode != 3));
        ui.set_files_open(self.windows.iter().any(|app| app.kind == 2 && !app.minimized && app.anim_mode != 3));
        ui.set_terminal_minimized(self.windows.iter().any(|app| app.kind == 0 && (app.minimized || app.anim_mode == 3)));
        ui.set_sysmon_minimized(self.windows.iter().any(|app| app.kind == 1 && (app.minimized || app.anim_mode == 3)));
        ui.set_files_minimized(self.windows.iter().any(|app| app.kind == 2 && (app.minimized || app.anim_mode == 3)));
    }

    // ====== refresh ======

    pub fn refresh(&self, ui: &VoidDesktop) {
        let mut surfaces = Vec::new();
        for app in self
            .windows
            .iter()
            .filter(|app| app.kind != self.focused)
            .chain(self.windows.iter().filter(|app| app.kind == self.focused))
        {
            if app.commits == 0 {
                continue;
            }
            surfaces.push(AppSurface {
                kind: app.kind,
                title: match app.kind {
                    1 => "System Monitor",
                    2 => "Files",
                    _ => "Terminale",
                }.into(),
                pixels: app.image.clone(),
                client_width: app.width as f32,
                client_height: app.height as f32,
                view_width: app.view_w,
                view_height: app.view_h,
                pos_x: app.pos_x,
                pos_y: app.pos_y,
                minimized: app.minimized,
                maximized: app.maximized,
                corner_top_radius: app.top_radius,
                corner_bottom_radius: app.bottom_radius,
                scale_progress: app.scale_progress,
                anim_kind: match app.anim_mode {
                    2 => 1,
                    3 => 2,
                    _ => 0,
                },
            });
        }
        self.model.set_vec(surfaces);
        self.sync_dock_indicators(ui);
        ui.set_focused_window(self.focused);
    }

    // ====== commits ======

    pub fn commits(&self, kind: i32) -> u64 {
        self.windows
            .iter()
            .find(|app| app.kind == kind)
            .map_or(0, |app| app.commits)
    }

    // ====== has_pending ======

    pub fn has_pending(&self) -> bool {
        self.windows.iter().any(|app| !app.pending.is_empty())
    }
}

impl Application {
    // ====== receive ======

    fn receive(&mut self, msg: GuiMessage, display_width: u32, display_height: u32) -> bool {
        if msg.kind == MSG_CREATE_WINDOW {
            if self.shm != 0
                || msg.e == 0
                || msg.f == 0
                || msg.e > display_width
                || msg.f > display_height
            {
                return false;
            }
            let bytes = u64::from(msg.e) * u64::from(msg.f) * 4;
            let mut view = libvoid::ShmView::default();
            if libvoid::shm_view(msg.a, &mut view) != 0 {
                return false;
            }
            if view.address < 0x1000_0000
                || bytes > view.bytes
                || view
                    .address
                    .checked_add(view.bytes)
                    .is_none_or(|end| end > 0x4000_0000)
            {
                let _ = libvoid::shm_unmap(msg.a);
                return false;
            }
            self.shm = msg.a;
            self.view = view;
            self.width = msg.e;
            self.height = msg.f;
            self.view_w = msg.e as f32;
            self.view_h = msg.f as f32;
            let max_x = ((display_width as f32) - (self.view_w + 2.0) - 16.0).max(0.0);
            let max_y = ((display_height as f32) - (self.view_h + 34.0) - 72.0).max(0.0);
            self.pos_x = self.pos_x.clamp(0.0, max_x);
            self.pos_y = self.pos_y.clamp(0.0, max_y);
            self.saved_x = self.pos_x;
            self.saved_y = self.pos_y;
            self.saved_w = self.view_w;
            self.saved_h = self.view_h;
            self.buffer = SharedPixelBuffer::<Rgb8Pixel>::new(self.width, self.height);
            self.image = Image::default();
            return true;
        }
        if msg.kind == MSG_RESIZE_WINDOW {
            if self.shm == 0
                || msg.a == 0
                || msg.e < 160
                || msg.f < 120
                || msg.e > display_width
                || msg.f > display_height
            {
                return false;
            }
            let bytes = u64::from(msg.e) * u64::from(msg.f) * 4;
            if msg.a != self.shm {
                let mut view = libvoid::ShmView::default();
                if libvoid::shm_view(msg.a, &mut view) != 0 {
                    return false;
                }
                if view.address < 0x1000_0000
                    || bytes > view.bytes
                    || view
                        .address
                        .checked_add(view.bytes)
                        .is_none_or(|end| end > 0x4000_0000)
                {
                    let _ = libvoid::shm_unmap(msg.a);
                    return false;
                }
                let old_shm = self.shm;
                self.shm = msg.a;
                self.view = view;
                let _ = libvoid::shm_unmap(old_shm);
            } else if bytes > self.view.bytes {
                return false;
            }
            self.width = msg.e;
            self.height = msg.f;
            self.image = Image::default();
            self.buffer = SharedPixelBuffer::new(0, 0);
            self.buffer = SharedPixelBuffer::<Rgb8Pixel>::new(self.width, self.height);
            return true;
        }
        if msg.kind != MSG_COMMIT_RECT
            || msg.win_id != 1
            || self.shm == 0
            || msg.c < 0
            || msg.d < 0
            || msg.e == 0
            || msg.f == 0
            || (msg.c as u64 + u64::from(msg.e)) > u64::from(self.width)
            || (msg.d as u64 + u64::from(msg.f)) > u64::from(self.height)
        {
            return false;
        }
        let rx = msg.c as usize;
        let ry = msg.d as usize;
        let rw = msg.e as usize;
        let rh = msg.f as usize;
        let stride = self.width as usize;
        self.image = Image::default();
        let dst = self.buffer.make_mut_slice();
        let src_base = self.view.address as *const u32;
        for row in ry..(ry + rh) {
            let row_off = row * stride + rx;
            for col in 0..rw {
                let idx = row_off + col;
                let source = unsafe { src_base.add(idx).read_volatile() };
                dst[idx] = Rgb8Pixel {
                    r: (source >> 16) as u8,
                    g: (source >> 8) as u8,
                    b: source as u8,
                };
            }
        }
        self.image = Image::from_rgb8(self.buffer.clone());
        if self.commits == 0 {
            if self.kind == 1 {
                let _ = libvoid::write_console(b"VoidOS desktop sysmon ready\n");
            } else {
                let _ = libvoid::write_console(b"VoidOS desktop terminal ready\n");
            }
        }
        self.commits += 1;
        true
    }
}
