use crate::{AppSurface, VoidDesktop};
use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::vec::Vec;
use libui::proto::{GuiMessage, MSG_COMMIT_RECT, MSG_CREATE_WINDOW, PROTO_WIRE_BYTES};
use slint::{Image, Model, Rgb8Pixel, SharedPixelBuffer, VecModel};

pub struct Application {
    kind: i32,
    child: libvoid::ConnectedChild,
    shm: u64,
    view: libvoid::ShmView,
    width: u32,
    height: u32,
    pos_x: f32,
    pos_y: f32,
    minimized: bool,
    anim_mode: u8,
    anim_start_ms: u64,
    scale_progress: f32,
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
        }
    }

    // ====== model ======

    pub fn model(&self) -> Rc<VecModel<AppSurface>> {
        self.model.clone()
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
            pos_x: init_x,
            pos_y: init_y,
            minimized: false,
            anim_mode: if self.verify { 0 } else { 1 },
            anim_start_ms: now_ms,
            scale_progress: if self.verify { 1.0 } else { 0.05 },
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
                // Ultra-smooth ease-out quartic
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
                // Smooth ease-in-out cubic
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
                    }
                }
                any_updated = true;
            }
            let kind = app.kind;
            let progress = app.scale_progress;
            for row in 0..self.model.row_count() {
                if let Some(mut surface) = self.model.row_data(row) {
                    if surface.kind == kind {
                        surface.scale_progress = progress;
                        surface.anim_kind = cur_anim_kind;
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
        }
        any_updated
    }

    // ====== hit_test ======

    // Returns (kind, in_titlebar_drag_zone, win_x, win_y) for the topmost visible window under (x, y).
    pub fn hit_test(&self, x: f32, y: f32) -> Option<(i32, bool, f32, f32)> {
        // Z-order matches refresh(): focused window is topmost, then others in reverse order.
        let ordered = self
            .windows
            .iter()
            .filter(|app| app.kind == self.focused)
            .chain(self.windows.iter().rev().filter(|app| app.kind != self.focused));
        for app in ordered {
            if app.minimized || app.commits == 0 || app.anim_mode == 2 || app.anim_mode == 3 {
                continue;
            }
            let total_w = app.width as f32 + 2.0;
            let total_h = app.height as f32 + 34.0;
            if x >= app.pos_x
                && x < app.pos_x + total_w
                && y >= app.pos_y
                && y < app.pos_y + total_h
            {
                let in_titlebar = y < app.pos_y + 32.0 && x < app.pos_x + (total_w - 88.0).max(0.0);
                return Some((app.kind, in_titlebar, app.pos_x, app.pos_y));
            }
        }
        None
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
                                if message.kind == MSG_COMMIT_RECT {
                                    // Drop model's Image reference before in-place buffer update so
                                    // SharedPixelBuffer::make_mut_slice() stays refcount == 1 (zero alloc).
                                    for row in 0..self.model.row_count() {
                                        if let Some(mut surface) = self.model.row_data(row) {
                                            if surface.kind == app.kind {
                                                surface.pixels = Image::default();
                                                self.model.set_row_data(row, surface);
                                                break;
                                            }
                                        }
                                    }
                                    pixels_updated = true;
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
                pos_x: app.pos_x,
                pos_y: app.pos_y,
                minimized: app.minimized,
                scale_progress: app.scale_progress,
                anim_kind: match app.anim_mode {
                    2 => 1,
                    3 => 2,
                    _ => 0,
                },
            });
        }
        self.model.set_vec(surfaces);
        ui.set_terminal_open(self.windows.iter().any(|app| app.kind == 0 && !app.minimized));
        ui.set_sysmon_open(self.windows.iter().any(|app| app.kind == 1 && !app.minimized));
        ui.set_files_open(self.windows.iter().any(|app| app.kind == 2 && !app.minimized));
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
                || msg.f > display_height.saturating_sub(160)
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
            self.buffer = SharedPixelBuffer::<Rgb8Pixel>::new(self.width, self.height);
            self.image = Image::default();
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
