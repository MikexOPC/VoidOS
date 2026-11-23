use libui::anim::RectAnim;
use libui::draw::{Canvas, Rect};
use libui::style::Theme;

pub const MAX_WINDOWS: usize = 8;
pub const TITLEBAR_HEIGHT: i32 = 28;

// ====== WindowSlot (Layer 1) ======

#[derive(Clone, Copy)]
pub struct WindowSlot {
    pub active: bool,
    pub focused: bool,
    pub rect: RectAnim,
    pub title: [u8; 32],
    pub title_len: usize,
    pub body_color: u32,
}

impl WindowSlot {
    pub const fn empty() -> Self {
        Self {
            active: false,
            focused: false,
            rect: RectAnim::settled(0, 0, 0, 0),
            title: [0u8; 32],
            title_len: 0,
            body_color: 0,
        }
    }

    pub fn open(x: i32, y: i32, w: i32, h: i32, title: &[u8], body_color: u32, focused: bool) -> Self {
        let mut slot = Self::empty();
        slot.active = true;
        slot.focused = focused;
        slot.rect = RectAnim::settled(x, y, w, h);
        let n = title.len().min(31);
        slot.title[..n].copy_from_slice(&title[..n]);
        slot.title_len = n;
        slot.body_color = body_color;
        slot
    }

    pub fn draw(&self, canvas: &mut Canvas<'_>, clip: Rect, theme: &Theme, now_ms: u64) {
        if !self.active {
            return;
        }
        let (wx, wy, ww, wh) = self.rect.sample(now_ms);
        let wr = Rect::new(wx, wy, ww, wh);
        if wr.intersect(clip).is_empty() {
            return;
        }
        let border_col = if self.focused { theme.window_border_focus } else { theme.window_border };
        let tb_col = if self.focused { theme.titlebar_focus } else { theme.titlebar_bg };
        canvas.fill_rounded_rect(wr, theme.window_radius, self.body_color);
        canvas.fill_rect(Rect::new(wr.x + 1, wr.y + 1, wr.w - 2, TITLEBAR_HEIGHT), tb_col);
        canvas.stroke_rect(wr, border_col);
        canvas.draw_text(wr.x + 12, wr.y + 6, &self.title[..self.title_len], theme.text_primary);
        let close_r = Rect::new(wr.right() - 22, wr.y + 6, 14, 14);
        canvas.fill_rounded_rect(close_r, 4, theme.danger);
    }
}
