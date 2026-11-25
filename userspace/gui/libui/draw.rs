use crate::anim::lerp_color_bgra;

// ====== Rect ======

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

impl Rect {
    pub const fn new(x: i32, y: i32, w: i32, h: i32) -> Self {
        Self { x, y, w, h }
    }

    #[inline]
    pub const fn is_empty(&self) -> bool {
        self.w <= 0 || self.h <= 0
    }

    #[inline]
    pub const fn right(&self) -> i32 {
        self.x.saturating_add(self.w)
    }

    #[inline]
    pub const fn bottom(&self) -> i32 {
        self.y.saturating_add(self.h)
    }

    #[inline]
    pub const fn contains(&self, px: i32, py: i32) -> bool {
        px >= self.x && py >= self.y && px < self.right() && py < self.bottom()
    }

    pub fn intersect(&self, other: Rect) -> Rect {
        let x0 = self.x.max(other.x);
        let y0 = self.y.max(other.y);
        let x1 = self.right().min(other.right());
        let y1 = self.bottom().min(other.bottom());
        if x1 <= x0 || y1 <= y0 {
            Rect::new(0, 0, 0, 0)
        } else {
            Rect::new(x0, y0, x1 - x0, y1 - y0)
        }
    }

    #[inline]
    pub const fn area(&self) -> u64 {
        if self.w <= 0 || self.h <= 0 {
            0
        } else {
            (self.w as u64) * (self.h as u64)
        }
    }

    pub fn union(&self, other: Rect) -> Rect {
        if self.is_empty() {
            return other;
        }
        if other.is_empty() {
            return *self;
        }
        let x0 = self.x.min(other.x);
        let y0 = self.y.min(other.y);
        let x1 = self.right().max(other.right());
        let y1 = self.bottom().max(other.bottom());
        Rect::new(x0, y0, x1 - x0, y1 - y0)
    }
}

// ====== DirtyList (bounded to 16 rects matching SYS_DISPLAY_PRESENT ceiling) ======

pub const MAX_DIRTY_RECTS: usize = 16;

#[derive(Clone, Copy, Debug)]
pub struct DirtyList {
    rects: [Rect; MAX_DIRTY_RECTS],
    count: usize,
}

impl DirtyList {
    pub const fn new() -> Self {
        Self {
            rects: [Rect::new(0, 0, 0, 0); MAX_DIRTY_RECTS],
            count: 0,
        }
    }

    pub fn clear(&mut self) {
        self.count = 0;
    }

    pub fn as_slice(&self) -> &[Rect] {
        &self.rects[..self.count]
    }

    pub fn total_area(&self) -> u64 {
        let mut total = 0u64;
        for r in &self.rects[..self.count] {
            total = total.saturating_add(r.area());
        }
        total
    }

    fn merge_waste(a: Rect, b: Rect) -> u64 {
        let u = a.union(b).area();
        let sum = a.area().saturating_add(b.area());
        u.saturating_sub(sum)
    }

    pub fn add(&mut self, rect: Rect, bounds: Rect) {
        let mut clipped = rect.intersect(bounds);
        if clipped.is_empty() {
            return;
        }
        // Merge with any overlapping existing rectangle (transitively to avoid duplicates).
        let mut idx = 0;
        while idx < self.count {
            if !self.rects[idx].intersect(clipped).is_empty() {
                clipped = self.rects[idx].union(clipped);
                self.count -= 1;
                self.rects[idx] = self.rects[self.count];
            } else {
                idx += 1;
            }
        }
        if self.count < MAX_DIRTY_RECTS {
            self.rects[self.count] = clipped;
            self.count += 1;
            return;
        }
        // Saturated (16 rects): merge the single pair with minimum pixel-area overhead
        // instead of collapsing the entire screen to fullscreen.
        let mut best_waste = u64::MAX;
        let mut best_union_area = u64::MAX;
        let mut best_i = 0usize;
        let mut best_j = MAX_DIRTY_RECTS; // MAX_DIRTY_RECTS represents `clipped`
        for i in 0..self.count {
            let w_new = Self::merge_waste(self.rects[i], clipped);
            let u_new = self.rects[i].union(clipped).area();
            if w_new < best_waste || (w_new == best_waste && u_new < best_union_area) {
                best_waste = w_new;
                best_union_area = u_new;
                best_i = i;
                best_j = MAX_DIRTY_RECTS;
            }
            for j in (i + 1)..self.count {
                let w_pair = Self::merge_waste(self.rects[i], self.rects[j]);
                let u_pair = self.rects[i].union(self.rects[j]).area();
                if w_pair < best_waste || (w_pair == best_waste && u_pair < best_union_area) {
                    best_waste = w_pair;
                    best_union_area = u_pair;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        if best_j == MAX_DIRTY_RECTS {
            self.rects[best_i] = self.rects[best_i].union(clipped);
        } else {
            self.rects[best_i] = self.rects[best_i].union(self.rects[best_j]);
            self.rects[best_j] = clipped;
        }
    }
}

// ====== alpha blending ======

#[inline]
pub fn blend_bgra(dst: u32, src: u32) -> u32 {
    let a = (src >> 24) & 0xFF;
    if a == 0xFF {
        return src;
    }
    if a == 0 {
        return dst;
    }
    let inv = 255 - a;
    let b = (((src & 0xFF) * a + (dst & 0xFF) * inv + 128) >> 8) & 0xFF;
    let g = ((((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * inv + 128) >> 8) & 0xFF;
    let r = ((((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * inv + 128) >> 8) & 0xFF;
    b | (g << 8) | (r << 16) | 0xFF00_0000
}

// ====== SIMD SSE2 4-pixel fill helper ======

#[inline]
fn fill_span_opaque(dst: &mut [u32], color: u32) {
    #[cfg(all(target_arch = "x86_64", target_feature = "sse2"))]
    {
        use core::arch::x86_64::{_mm_set1_epi32, _mm_storeu_si128, __m128i};
        let mut idx = 0usize;
        let len = dst.len();
        let ptr = dst.as_mut_ptr();
        unsafe {
            let vec = _mm_set1_epi32(color as i32);
            while idx + 4 <= len {
                _mm_storeu_si128(ptr.add(idx) as *mut __m128i, vec);
                idx += 4;
            }
        }
        for slot in &mut dst[idx..] {
            *slot = color;
        }
    }
    #[cfg(not(all(target_arch = "x86_64", target_feature = "sse2")))]
    {
        for slot in dst {
            *slot = color;
        }
    }
}

// ====== Canvas ======

pub struct Canvas<'a> {
    pub pixels: &'a mut [u32],
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub clip: Rect,
}

impl<'a> Canvas<'a> {
    pub fn new(pixels: &'a mut [u32], width: u32, height: u32, stride: u32) -> Self {
        let clip = Rect::new(0, 0, width as i32, height as i32);
        Self {
            pixels,
            width,
            height,
            stride,
            clip,
        }
    }

    pub fn set_clip(&mut self, rect: Rect) {
        let full = Rect::new(0, 0, self.width as i32, self.height as i32);
        self.clip = rect.intersect(full);
    }

    #[inline]
    pub fn put_pixel(&mut self, x: i32, y: i32, color: u32) {
        if !self.clip.contains(x, y) {
            return;
        }
        let idx = (y as usize) * (self.stride as usize) + (x as usize);
        if let Some(slot) = self.pixels.get_mut(idx) {
            *slot = blend_bgra(*slot, color);
        }
    }

    pub fn fill_rect(&mut self, rect: Rect, color: u32) {
        let r = rect.intersect(self.clip);
        if r.is_empty() {
            return;
        }
        let alpha = (color >> 24) & 0xFF;
        if alpha == 0 {
            return;
        }
        let stride = self.stride as usize;
        let x0 = r.x as usize;
        let x1 = r.right() as usize;
        for y in r.y..r.bottom() {
            let row_start = (y as usize) * stride + x0;
            let row_end = (y as usize) * stride + x1;
            if row_end > self.pixels.len() {
                break;
            }
            if alpha == 0xFF {
                fill_span_opaque(&mut self.pixels[row_start..row_end], color);
            } else {
                for slot in &mut self.pixels[row_start..row_end] {
                    *slot = blend_bgra(*slot, color);
                }
            }
        }
    }

    pub fn fill_vertical_gradient(&mut self, rect: Rect, top_color: u32, bottom_color: u32) {
        let r = rect.intersect(self.clip);
        if r.is_empty() || rect.h <= 0 {
            return;
        }
        let stride = self.stride as usize;
        let x0 = r.x as usize;
        let x1 = r.right() as usize;
        let denom = (rect.h - 1).max(1) as u64;
        for y in r.y..r.bottom() {
            let dy = (y - rect.y).max(0) as u64;
            let t_fp16 = ((dy << 16) / denom).min(65_536) as i32;
            let color = lerp_color_bgra(top_color, bottom_color, t_fp16) | 0xFF00_0000;
            let row_start = (y as usize) * stride + x0;
            let row_end = (y as usize) * stride + x1;
            if row_end > self.pixels.len() {
                break;
            }
            fill_span_opaque(&mut self.pixels[row_start..row_end], color);
        }
    }

    pub fn fill_rounded_rect(&mut self, rect: Rect, radius: u32, color: u32) {
        if rect.is_empty() {
            return;
        }
        let rad = (radius as i32).min(rect.w / 2).min(rect.h / 2).max(0);
        if rad <= 1 {
            self.fill_rect(rect, color);
            return;
        }
        // Center body
        self.fill_rect(Rect::new(rect.x, rect.y + rad, rect.w, rect.h - 2 * rad), color);
        // Rounded top and bottom caps using integer circle inset
        let r2 = rad * rad;
        for dy in 0..rad {
            let cy = rad - 1 - dy;
            let mut inset = rad;
            while inset > 0 {
                let cx = inset - 1;
                if cx * cx + cy * cy <= r2 {
                    break;
                }
                inset -= 1;
            }
            let trim = rad - inset;
            let span_w = rect.w - 2 * trim;
            if span_w > 0 {
                self.fill_rect(Rect::new(rect.x + trim, rect.y + dy, span_w, 1), color);
                self.fill_rect(Rect::new(rect.x + trim, rect.bottom() - 1 - dy, span_w, 1), color);
            }
        }
    }

    pub fn stroke_rect(&mut self, rect: Rect, color: u32) {
        if rect.w <= 0 || rect.h <= 0 {
            return;
        }
        self.fill_rect(Rect::new(rect.x, rect.y, rect.w, 1), color);
        self.fill_rect(Rect::new(rect.x, rect.bottom() - 1, rect.w, 1), color);
        self.fill_rect(Rect::new(rect.x, rect.y, 1, rect.h), color);
        self.fill_rect(Rect::new(rect.right() - 1, rect.y, 1, rect.h), color);
    }

    pub fn blit_surface(&mut self, dst_x: i32, dst_y: i32, src: &[u32], src_w: u32, src_h: u32, src_stride: u32) {
        let dst_rect = Rect::new(dst_x, dst_y, src_w as i32, src_h as i32).intersect(self.clip);
        if dst_rect.is_empty() {
            return;
        }
        let dstride = self.stride as usize;
        let sstride = src_stride as usize;
        let copy_w = dst_rect.w as usize;
        let src_x0 = (dst_rect.x - dst_x) as usize;
        for row in 0..(dst_rect.h as usize) {
            let sy = ((dst_rect.y - dst_y) as usize) + row;
            let dy = (dst_rect.y as usize) + row;
            let s_off = sy * sstride + src_x0;
            let d_off = dy * dstride + (dst_rect.x as usize);
            if s_off + copy_w <= src.len() && d_off + copy_w <= self.pixels.len() {
                self.pixels[d_off..d_off + copy_w].copy_from_slice(&src[s_off..s_off + copy_w]);
            }
        }
    }

    pub fn draw_char(&mut self, x: i32, y: i32, ch: u8, color: u32) {
        let glyph = glyph_8x8(ch);
        for (row, &bits) in glyph.iter().enumerate() {
            let py = y + (row as i32) * 2;
            for col in 0..8 {
                if (bits & (0x80 >> col)) != 0 {
                    let px = x + col;
                    self.put_pixel(px, py, color);
                    self.put_pixel(px, py + 1, color);
                }
            }
        }
    }

    pub fn draw_text(&mut self, mut x: i32, y: i32, text: &[u8], color: u32) {
        for &b in text {
            if b == 0 {
                break;
            }
            self.draw_char(x, y, b, color);
            x += 8;
        }
    }
}

// ====== compact 8x8 -> 8x16 bitmap font (printable ASCII) ======

fn glyph_8x8(ch: u8) -> [u8; 8] {
    match ch {
        b' ' => [0x00; 8],
        b'!' => [0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00],
        b'"' => [0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00],
        b'#' => [0x36, 0x7F, 0x36, 0x36, 0x7F, 0x36, 0x00, 0x00],
        b'%' => [0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00],
        b'(' => [0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00],
        b')' => [0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00],
        b'*' => [0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00],
        b'+' => [0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00],
        b',' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30],
        b'-' => [0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00],
        b'.' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00],
        b'/' => [0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00],
        b'0' => [0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00],
        b'1' => [0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00],
        b'2' => [0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00],
        b'3' => [0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00],
        b'4' => [0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00],
        b'5' => [0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00],
        b'6' => [0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00],
        b'7' => [0x7E, 0x66, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x00],
        b'8' => [0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00],
        b'9' => [0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00],
        b':' => [0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00],
        b';' => [0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00],
        b'<' => [0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00],
        b'=' => [0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00],
        b'>' => [0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00],
        b'?' => [0x3C, 0x66, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00],
        b'[' => [0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00],
        b']' => [0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00],
        b'_' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00],
        b'A' | b'a' => [0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00],
        b'B' | b'b' => [0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00],
        b'C' | b'c' => [0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00],
        b'D' | b'd' => [0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00],
        b'E' | b'e' => [0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00],
        b'F' | b'f' => [0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00],
        b'G' | b'g' => [0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00],
        b'H' | b'h' => [0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00],
        b'I' | b'i' => [0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00],
        b'J' | b'j' => [0x1E, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38, 0x00],
        b'K' | b'k' => [0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00],
        b'L' | b'l' => [0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00],
        b'M' | b'm' => [0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00],
        b'N' | b'n' => [0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00],
        b'O' | b'o' => [0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        b'P' | b'p' => [0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00],
        b'Q' | b'q' => [0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00],
        b'R' | b'r' => [0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00],
        b'S' | b's' => [0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00],
        b'T' | b't' => [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00],
        b'U' | b'u' => [0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        b'V' | b'v' => [0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00],
        b'W' | b'w' => [0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00],
        b'X' | b'x' => [0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00],
        b'Y' | b'y' => [0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00],
        b'Z' | b'z' => [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00],
        _ => [0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00],
    }
}
