use crate::cursor_data::{CURSOR_SIZE, CURSOR_SPRITE};
use crate::BgrxPixel;

pub struct CursorOverlay {
    drawn: bool,
    x: i32,
    y: i32,
    w: usize,
    h: usize,
    saved: [u32; CURSOR_SIZE * CURSOR_SIZE],
}

impl CursorOverlay {
    // ====== new ======

    pub const fn new() -> Self {
        Self {
            drawn: false,
            x: 0,
            y: 0,
            w: 0,
            h: 0,
            saved: [0; CURSOR_SIZE * CURSOR_SIZE],
        }
    }

    // ====== pos ======

    pub fn pos(&self) -> (i32, i32, bool) {
        (self.x, self.y, self.drawn)
    }

    // ====== restore ======

    pub fn restore(&mut self, fb: &mut [BgrxPixel], stride: usize) -> Option<libvoid::DisplayRect> {
        if !self.drawn || self.w == 0 || self.h == 0 {
            self.drawn = false;
            return None;
        }
        let x0 = self.x as usize;
        let y0 = self.y as usize;
        for row in 0..self.h {
            let dst_off = (y0 + row) * stride + x0;
            let src_off = row * CURSOR_SIZE;
            for col in 0..self.w {
                fb[dst_off + col].0 = self.saved[src_off + col];
            }
        }
        self.drawn = false;
        Some(libvoid::DisplayRect {
            x: x0 as u32,
            y: y0 as u32,
            width: self.w as u32,
            height: self.h as u32,
        })
    }

    // ====== draw ======

    pub fn draw(
        &mut self,
        fb: &mut [BgrxPixel],
        stride: usize,
        max_w: u32,
        max_h: u32,
        x: i32,
        y: i32,
    ) -> Option<libvoid::DisplayRect> {
        let x0 = x.clamp(0, max_w.saturating_sub(1) as i32) as usize;
        let y0 = y.clamp(0, max_h.saturating_sub(1) as i32) as usize;
        let w = CURSOR_SIZE.min((max_w as usize).saturating_sub(x0));
        let h = CURSOR_SIZE.min((max_h as usize).saturating_sub(y0));
        if w == 0 || h == 0 {
            return None;
        }
        for row in 0..h {
            let fb_off = (y0 + row) * stride + x0;
            let sp_off = row * CURSOR_SIZE;
            for col in 0..w {
                self.saved[sp_off + col] = fb[fb_off + col].0;
                let s = CURSOR_SPRITE[sp_off + col];
                let a = (s >> 24) & 0xFF;
                if a == 0 {
                    continue;
                }
                if a == 255 {
                    fb[fb_off + col].0 = (s & 0x00FF_FFFF) | 0xFF00_0000;
                    continue;
                }
                let d = fb[fb_off + col].0;
                let inv = 255 - a;
                let sr = (s >> 16) & 0xFF;
                let sg = (s >> 8) & 0xFF;
                let sb = s & 0xFF;
                let dr = (d >> 16) & 0xFF;
                let dg = (d >> 8) & 0xFF;
                let db = d & 0xFF;
                let r = (sr * a + dr * inv + 128) >> 8;
                let g = (sg * a + dg * inv + 128) >> 8;
                let b = (sb * a + db * inv + 128) >> 8;
                fb[fb_off + col].0 = 0xFF00_0000 | (r << 16) | (g << 8) | b;
            }
        }
        self.x = x0 as i32;
        self.y = y0 as i32;
        self.w = w;
        self.h = h;
        self.drawn = true;
        Some(libvoid::DisplayRect {
            x: x0 as u32,
            y: y0 as u32,
            width: w as u32,
            height: h as u32,
        })
    }
}
