// ====== costanti SP 800-90B ======

pub const Z_99         : f64 = 2.576;
pub const SEARCH_STEPS : u32 = 200;

// ====== upper_bound ======

pub fn upper_bound(proportion: f64, samples: usize) -> f64 {
    let spread = (proportion * (1.0 - proportion) / (samples as f64 - 1.0)).sqrt();
    (proportion + Z_99 * spread).min(1.0)
}

// ====== global_performance ======

pub fn global_performance(correct: usize, total: usize) -> f64 {
    if correct == 0 {
        return 1.0 - 0.01f64.powf(1.0 / total as f64);
    }
    upper_bound(correct as f64 / total as f64, total)
}

// ====== local_performance ======

fn no_run_probability(p: f64, run: u32, total: usize) -> f64 {
    let q = 1.0 - p;
    let r = f64::from(run);
    let mut x = 1.0f64;
    for _ in 0..10 {
        x = 1.0 + q * p.powf(r) * x.powf(r + 1.0);
    }
    let logarithm = (1.0 - p * x).ln() - ((r + 1.0 - r * x) * q).ln() - (total as f64 + 1.0) * x.ln();
    logarithm.exp()
}

pub fn local_performance(run: u32, total: usize) -> f64 {
    let (mut low, mut high) = (0.0f64, 1.0f64);
    for _ in 0..SEARCH_STEPS {
        let middle = (low + high) / 2.0;
        if no_run_probability(middle, run, total) > 0.99 {
            low = middle;
        } else {
            high = middle;
        }
    }
    low
}

// ====== longest_run ======

pub fn longest_run(correct: &[bool]) -> u32 {
    let (mut best, mut current) = (0u32, 0u32);
    for hit in correct {
        current = if *hit { current + 1 } else { 0 };
        best = best.max(current);
    }
    best
}

// ====== predictor_entropy ======

pub fn predictor_entropy(correct: &[bool], alphabet: usize) -> f64 {
    let total = correct.len();
    let hits = correct.iter().filter(|hit| **hit).count();
    let global = global_performance(hits, total);
    let local = local_performance(longest_run(correct) + 1, total);
    -global.max(local).max(1.0 / alphabet as f64).log2()
}

// ====== bisect ======

pub fn bisect_decreasing(function: impl Fn(f64) -> f64, target: f64, low: f64, high: f64) -> Option<f64> {
    if target > function(low) || target < function(high) {
        return None;
    }
    let (mut low, mut high) = (low, high);
    for _ in 0..SEARCH_STEPS {
        let middle = (low + high) / 2.0;
        if function(middle) > target {
            low = middle;
        } else {
            high = middle;
        }
    }
    Some((low + high) / 2.0)
}
