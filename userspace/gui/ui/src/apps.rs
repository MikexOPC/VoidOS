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
    buffer: SharedPixelBuffer<Rgb8Pixel>,
    image: Image,
    pending: VecDeque<[u8; PROTO_WIRE_BYTES]>,
    pub commits: u64,
    probe_read_only: bool,
}

pub struct Applications {
    windows: Vec<Application>,
    model: Rc<VecModel<AppSurface>>,
    pub focused: i32,
    verify: bool,
}

impl Applications {
    // ====== new ======

    pub fn new(verify: bool) -> Self {
        Self {
            windows: Vec::new(),
            model: Rc::new(VecModel::default()),
            focused: 0,
            verify,
        }
    }

    // ====== model ======

    pub fn model(&self) -> Rc<VecModel<AppSurface>> {
        self.model.clone()
    }

    // ====== launch ======

    pub fn launch(&mut self, kind: i32) -> bool {
        if kind != 0 {
            return false;
        }
        self.focused = kind;
        if self.windows.iter().any(|app| app.kind == kind) {
            return true;
        }
        let budget = libvoid::SpawnBudget {
            max_image_pages: 128,
            max_open_files: 8,
            cpu_budget_ms: 2000,
            flags: 0,
        };
        let mut child = libvoid::ConnectedChild::default();
        if libvoid::spawn_connected(b"/bin/terminal.elf", budget, &mut child) <= 0 {
            return false;
        }
        self.windows.push(Application {
            kind,
            child,
            shm: 0,
            view: libvoid::ShmView::default(),
            width: 0,
            height: 0,
            buffer: SharedPixelBuffer::new(0, 0),
            image: Image::default(),
            pending: VecDeque::new(),
            commits: 0,
            probe_read_only: self.verify,
        });
        true
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
        let mut changed = false;
        let mut healthy = true;
        let mut dead = Vec::new();
        for app in &mut self.windows {
            if app.probe_read_only && app.shm != 0 {
                let result = libvoid::raw_syscall(
                    libvoid::SYS_RECV,
                    app.child.endpoint,
                    app.view.address,
                    PROTO_WIRE_BYTES as u64,
                );
                if result == libvoid::ERR_WOULD_BLOCK {
                    continue;
                }
                if result != -2 {
                    healthy = false;
                    dead.push(app.kind);
                    continue;
                }
                app.probe_read_only = false;
                let _ = libvoid::write_console(b"VoidOS gui surface read-only ok\n");
            }
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
                    }
                    app.receive(message, display_width, display_height)
                });
                if !valid {
                    healthy = false;
                    dead.push(app.kind);
                    continue;
                }
                changed = true;
            } else if result == libvoid::ERR_PEER_CLOSED {
                dead.push(app.kind);
                continue;
            } else if result != libvoid::ERR_WOULD_BLOCK {
                healthy = false;
                dead.push(app.kind);
                continue;
            }
            if let Some(wire) = app.pending.front() {
                let result = libvoid::ipc_send(app.child.endpoint, wire);
                if result == PROTO_WIRE_BYTES as i64 {
                    app.pending.pop_front();
                } else if result != libvoid::ERR_WOULD_BLOCK {
                    healthy = false;
                    dead.push(app.kind);
                }
            }
        }
        for kind in dead {
            if !self.close(kind) {
                return false;
            }
            changed = true;
        }
        if changed {
            self.refresh(ui);
        }
        healthy
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
                title: "Terminale".into(),
                pixels: app.image.clone(),
                client_width: app.width as f32,
                client_height: app.height as f32,
            });
        }
        self.model.set_vec(surfaces);
        ui.set_terminal_open(self.windows.iter().any(|app| app.kind == 0));
        ui.set_sysmon_open(false);
        ui.set_focused_window(self.focused);
    }

    // ====== commits ======

    pub fn commits(&self, kind: i32) -> u64 {
        self.windows
            .iter()
            .find(|app| app.kind == kind)
            .map_or(0, |app| app.commits)
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
            let _ = libvoid::write_console(b"VoidOS desktop terminal ready\n");
        }
        self.commits += 1;
        true
    }
}
