use libui::draw::{Canvas, Rect};
use libui::style::Theme;

// ====== draw_background (Layer 0) ======

pub fn draw_background(canvas: &mut Canvas<'_>, theme: &Theme) {
    let full = Rect::new(0, 0, canvas.width as i32, canvas.height as i32);
    canvas.fill_vertical_gradient(full, theme.bg_top, theme.bg_bottom);
    canvas.draw_text(16, 12, b"VoidOS", theme.text_muted);
}
