use libui::anim::{Easing, RectAnim};
use libui::draw::{Canvas, Rect};
use libui::style::{DockEdge, DockShape, Theme, rgba};

// ====== DockLayer (Layer 2: dynamic shape, edge, and morph animations) ======

pub struct DockLayer {
    pub edge: DockEdge,
    pub shape: DockShape,
    pub thickness: i32,
    pub anim: RectAnim,
    pub hover_item: i32,
}

impl DockLayer {
    pub fn target_rect(screen_w: i32, screen_h: i32, edge: DockEdge, shape: DockShape, thickness: i32) -> Rect {
        let margin = 12;
        match edge {
            DockEdge::Bottom => match shape {
                DockShape::Floating => {
                    let w = 360.min(screen_w - 24).max(160);
                    Rect::new((screen_w - w) / 2, screen_h - thickness - margin, w, thickness)
                }
                DockShape::Bar => Rect::new(0, screen_h - thickness, screen_w, thickness),
                DockShape::Compact => {
                    let w = 220.min(screen_w - 24).max(140);
                    let h = (thickness - 8).max(30);
                    Rect::new((screen_w - w) / 2, screen_h - h - margin, w, h)
                }
            },
            DockEdge::Top => match shape {
                DockShape::Floating => {
                    let w = 360.min(screen_w - 24).max(160);
                    Rect::new((screen_w - w) / 2, margin, w, thickness)
                }
                DockShape::Bar => Rect::new(0, 0, screen_w, thickness),
                DockShape::Compact => {
                    let w = 220.min(screen_w - 24).max(140);
                    let h = (thickness - 8).max(30);
                    Rect::new((screen_w - w) / 2, margin, w, h)
                }
            },
            DockEdge::Left => {
                let h = match shape {
                    DockShape::Bar => screen_h,
                    DockShape::Floating => 280.min(screen_h - 24).max(140),
                    DockShape::Compact => 180.min(screen_h - 24).max(120),
                };
                let y = if shape == DockShape::Bar { 0 } else { (screen_h - h) / 2 };
                let x = if shape == DockShape::Bar { 0 } else { margin };
                Rect::new(x, y, thickness + 16, h)
            }
            DockEdge::Right => {
                let h = match shape {
                    DockShape::Bar => screen_h,
                    DockShape::Floating => 280.min(screen_h - 24).max(140),
                    DockShape::Compact => 180.min(screen_h - 24).max(120),
                };
                let y = if shape == DockShape::Bar { 0 } else { (screen_h - h) / 2 };
                let w = thickness + 16;
                let x = if shape == DockShape::Bar { screen_w - w } else { screen_w - w - margin };
                Rect::new(x, y, w, h)
            }
        }
    }

    pub fn new(screen_w: i32, screen_h: i32, theme: &Theme) -> Self {
        let r = Self::target_rect(screen_w, screen_h, theme.dock_edge, theme.dock_shape, theme.dock_thickness as i32);
        Self {
            edge: theme.dock_edge,
            shape: theme.dock_shape,
            thickness: theme.dock_thickness as i32,
            anim: RectAnim::settled(r.x, r.y, r.w, r.h),
            hover_item: -1,
        }
    }

    pub fn morph_to(&mut self, now_ms: u64, screen_w: i32, screen_h: i32, edge: DockEdge, shape: DockShape, dur_ms: u32) {
        self.edge = edge;
        self.shape = shape;
        let r = Self::target_rect(screen_w, screen_h, edge, shape, self.thickness);
        self.anim.retarget(now_ms, r.x, r.y, r.w, r.h, dur_ms, Easing::EaseOutBack);
    }

    pub fn cycle_form(&mut self, now_ms: u64, screen_w: i32, screen_h: i32, dur_ms: u32) {
        let (next_edge, next_shape) = match (self.edge, self.shape) {
            (DockEdge::Bottom, DockShape::Floating) => (DockEdge::Bottom, DockShape::Bar),
            (DockEdge::Bottom, DockShape::Bar) => (DockEdge::Bottom, DockShape::Compact),
            (DockEdge::Bottom, DockShape::Compact) => (DockEdge::Left, DockShape::Floating),
            (DockEdge::Left, _) => (DockEdge::Top, DockShape::Bar),
            _ => (DockEdge::Bottom, DockShape::Floating),
        };
        self.morph_to(now_ms, screen_w, screen_h, next_edge, next_shape, dur_ms);
    }

    pub fn current_rect(&self, now_ms: u64) -> Rect {
        let (x, y, w, h) = self.anim.sample(now_ms);
        Rect::new(x, y, w, h)
    }

    pub fn draw(&self, canvas: &mut Canvas<'_>, clip: Rect, theme: &Theme, now_ms: u64) {
        let dr = self.current_rect(now_ms);
        if dr.intersect(clip).is_empty() {
            return;
        }
        let radius = if self.shape == DockShape::Bar { 0 } else { theme.dock_radius };
        canvas.fill_rounded_rect(dr, radius, theme.dock_bg);
        canvas.stroke_rect(dr, theme.dock_border);
        let labels: [&[u8]; 3] = [b"TERM", b"SYS", b"DOCK"];
        let horizontal = matches!(self.edge, DockEdge::Bottom | DockEdge::Top);
        for (i, label) in labels.iter().enumerate() {
            let btn = if horizontal {
                let bw = 64;
                let gap = 10;
                let total = 3 * bw + 2 * gap;
                let start_x = dr.x + (dr.w - total) / 2;
                Rect::new(start_x + (i as i32) * (bw + gap), dr.y + 8, bw, (dr.h - 16).max(20))
            } else {
                let bh = 36;
                let gap = 10;
                let total = 3 * bh + 2 * gap;
                let start_y = dr.y + (dr.h - total) / 2;
                Rect::new(dr.x + 8, start_y + (i as i32) * (bh + gap), (dr.w - 16).max(24), bh)
            };
            let bg = if self.hover_item == i as i32 {
                theme.accent_hover
            } else if i == 2 {
                rgba(0x28, 0x34, 0x4A, 0xF0)
            } else {
                theme.accent
            };
            canvas.fill_rounded_rect(btn, 6, bg);
            canvas.draw_text(btn.x + 12, btn.y + 8, label, theme.text_primary);
        }
    }
}
