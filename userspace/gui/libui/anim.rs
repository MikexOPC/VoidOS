// ====== easing curves (16.16 fixed point, 0..=65536) ======

pub const FP_ONE: u32 = 65_536;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Easing {
    Linear,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutBack,
}

// ====== eval_easing ======

pub fn eval_easing(easing: Easing, t_fp16: u32) -> i32 {
    let t = t_fp16.min(FP_ONE) as i64;
    let one = FP_ONE as i64;
    match easing {
        Easing::Linear => t as i32,
        Easing::EaseOutCubic => {
            // 1 - (1 - t)^3
            let inv = one - t;
            let inv2 = (inv * inv) >> 16;
            let inv3 = (inv2 * inv) >> 16;
            (one - inv3) as i32
        }
        Easing::EaseInOutCubic => {
            if t < (one / 2) {
                // 4 * t^3
                let t2 = (t * t) >> 16;
                let t3 = (t2 * t) >> 16;
                (4 * t3) as i32
            } else {
                // 1 - ((-2t + 2)^3) / 2
                let u = 2 * one - 2 * t;
                let u2 = (u * u) >> 16;
                let u3 = (u2 * u) >> 16;
                (one - (u3 / 2)) as i32
            }
        }
        Easing::EaseOutBack => {
            // Subtle overshoot curve: c1 = 1.35 (~88474), c3 = 2.35 (~154010)
            let c1: i64 = 88_474;
            let c3: i64 = one + c1;
            let u = t - one;
            let u2 = (u * u) >> 16;
            let u3 = (u2 * u) >> 16;
            let term1 = (c3 * u3) >> 16;
            let term2 = (c1 * u2) >> 16;
            (one + term1 + term2) as i32
        }
    }
}

// ====== lerp_i32 ======

#[inline]
pub fn lerp_i32(start: i32, end: i32, factor_fp16: i32) -> i32 {
    let delta = (end as i64) - (start as i64);
    let step = (delta * (factor_fp16 as i64)) >> 16;
    (start as i64 + step) as i32
}

// ====== lerp_color_bgra ======

#[inline]
pub fn lerp_color_bgra(c0: u32, c1: u32, factor_fp16: i32) -> u32 {
    let f = factor_fp16.clamp(0, FP_ONE as i32) as u32;
    let inv = FP_ONE - f;
    let b = (((c0 & 0xFF) * inv + (c1 & 0xFF) * f + 32_768) >> 16) & 0xFF;
    let g = ((((c0 >> 8) & 0xFF) * inv + ((c1 >> 8) & 0xFF) * f + 32_768) >> 16) & 0xFF;
    let r = ((((c0 >> 16) & 0xFF) * inv + ((c1 >> 16) & 0xFF) * f + 32_768) >> 16) & 0xFF;
    let a = ((((c0 >> 24) & 0xFF) * inv + ((c1 >> 24) & 0xFF) * f + 32_768) >> 16) & 0xFF;
    b | (g << 8) | (r << 16) | (a << 24)
}

// ====== Tween ======

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Tween {
    pub start_val: i32,
    pub end_val: i32,
    pub start_ms: u64,
    pub duration_ms: u32,
    pub easing: Easing,
    pub active: bool,
}

impl Tween {
    pub const fn settled(value: i32) -> Self {
        Self {
            start_val: value,
            end_val: value,
            start_ms: 0,
            duration_ms: 0,
            easing: Easing::EaseOutCubic,
            active: false,
        }
    }

    pub fn retarget(&mut self, now_ms: u64, target: i32, duration_ms: u32, easing: Easing) {
        let current = self.sample(now_ms);
        if current == target || duration_ms == 0 {
            self.start_val = target;
            self.end_val = target;
            self.start_ms = now_ms;
            self.duration_ms = 0;
            self.easing = easing;
            self.active = false;
            return;
        }
        self.start_val = current;
        self.end_val = target;
        self.start_ms = now_ms;
        self.duration_ms = duration_ms;
        self.easing = easing;
        self.active = true;
    }

    pub fn sample(&self, now_ms: u64) -> i32 {
        if !self.active || self.duration_ms == 0 {
            return self.end_val;
        }
        let elapsed = now_ms.saturating_sub(self.start_ms);
        if elapsed >= self.duration_ms as u64 {
            return self.end_val;
        }
        let t_fp16 = ((elapsed << 16) / (self.duration_ms as u64)) as u32;
        let eased = eval_easing(self.easing, t_fp16);
        lerp_i32(self.start_val, self.end_val, eased)
    }

    pub fn is_done(&self, now_ms: u64) -> bool {
        !self.active || self.duration_ms == 0 || now_ms.saturating_sub(self.start_ms) >= self.duration_ms as u64
    }

    pub fn settle_if_done(&mut self, now_ms: u64) -> bool {
        if self.is_done(now_ms) {
            self.start_val = self.end_val;
            self.active = false;
            return true;
        }
        false
    }
}

// ====== RectAnim ======

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RectAnim {
    pub x: Tween,
    pub y: Tween,
    pub w: Tween,
    pub h: Tween,
}

impl RectAnim {
    pub const fn settled(x: i32, y: i32, w: i32, h: i32) -> Self {
        Self {
            x: Tween::settled(x),
            y: Tween::settled(y),
            w: Tween::settled(w),
            h: Tween::settled(h),
        }
    }

    pub fn retarget(&mut self, now_ms: u64, x: i32, y: i32, w: i32, h: i32, duration_ms: u32, easing: Easing) {
        self.x.retarget(now_ms, x, duration_ms, easing);
        self.y.retarget(now_ms, y, duration_ms, easing);
        self.w.retarget(now_ms, w, duration_ms, easing);
        self.h.retarget(now_ms, h, duration_ms, easing);
    }

    pub fn sample(&self, now_ms: u64) -> (i32, i32, i32, i32) {
        (
            self.x.sample(now_ms),
            self.y.sample(now_ms),
            self.w.sample(now_ms),
            self.h.sample(now_ms),
        )
    }

    pub fn is_animating(&self, now_ms: u64) -> bool {
        !self.x.is_done(now_ms)
            || !self.y.is_done(now_ms)
            || !self.w.is_done(now_ms)
            || !self.h.is_done(now_ms)
    }

    pub fn tick(&mut self, now_ms: u64) {
        self.x.settle_if_done(now_ms);
        self.y.settle_if_done(now_ms);
        self.w.settle_if_done(now_ms);
        self.h.settle_if_done(now_ms);
    }
}
