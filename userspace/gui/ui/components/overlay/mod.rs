use libui::draw::{Canvas, Rect};
use libui::style::{Theme, rgba};

// ====== draw_overlay (Layer 3: notifications, tooltips, status pills) ======

pub fn draw_overlay(canvas: &mut Canvas<'_>, clip: Rect, theme: &Theme, text: &[u8]) {
    if text.is_empty() {
        return;
    }
    let ow = (text.len() as i32) * 8 + 20;
    let or = Rect::new((canvas.width as i32 - ow) / 2, 16, ow, 28);
    if or.intersect(clip).is_empty() {
        return;
    }
    canvas.fill_rounded_rect(or, 8, rgba(0x12, 0x18, 0x24, 0xE8));
    canvas.stroke_rect(or, theme.accent);
    canvas.draw_text(or.x + 10, or.y + 6, text, theme.text_primary);
}
