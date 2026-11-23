use libui::draw::{Canvas, Rect};
use libui::style::rgb;

pub const CURSOR_W: i32 = 12;
pub const CURSOR_H: i32 = 16;

// ====== draw_cursor (Layer 4: software mouse pointer) ======

pub fn draw_cursor(canvas: &mut Canvas<'_>, clip: Rect, cx: i32, cy: i32) {
    let cr = Rect::new(cx, cy, CURSOR_W, CURSOR_H);
    if cr.intersect(clip).is_empty() {
        return;
    }
    const SHAPE: [u16; 16] = [
        0b1000_0000_0000,
        0b1100_0000_0000,
        0b1110_0000_0000,
        0b1111_0000_0000,
        0b1111_1000_0000,
        0b1111_1100_0000,
        0b1111_1110_0000,
        0b1111_1111_0000,
        0b1111_1111_1000,
        0b1111_1100_0000,
        0b1101_1100_0000,
        0b1000_1110_0000,
        0b0000_1110_0000,
        0b0000_0111_0000,
        0b0000_0111_0000,
        0b0000_0010_0000,
    ];
    let white = rgb(0xFF, 0xFF, 0xFF);
    let dark = rgb(0x10, 0x14, 0x1C);
    for (row, &bits) in SHAPE.iter().enumerate() {
        for col in 0..12 {
            if (bits & (1 << (11 - col))) != 0 {
                let is_edge = col == 0 || row == 0 || (bits & (1 << (11 - col - 1))) == 0;
                canvas.put_pixel(cx + col, cy + row as i32, if is_edge { white } else { dark });
            }
        }
    }
}
