use crate::statistics::{Z_99, bisect_decreasing};

// ====== statistiche campionarie ======

fn mean_and_deviation(values: &[f64]) -> (f64, f64) {
    let count = values.len() as f64;
    let mean = values.iter().sum::<f64>() / count;
    let variance = values.iter().map(|value| (value - mean).powi(2)).sum::<f64>() / (count - 1.0);
    (mean, variance.sqrt())
}

// ====== collision (6.3.2) ======

fn collision_mean(p: f64) -> f64 {
    let q = 1.0 - p;
    let gamma = q + 2.0 * q * q + 2.0 * q * q * q;
    let difference = 0.5 * (1.0 / p - 1.0 / q);
    p / (q * q) * (1.0 + difference) * gamma - p / q * difference
}

pub fn collision_times(bits: &[u8]) -> Vec<f64> {
    let mut times = Vec::new();
    let mut index = 0;
    while index < bits.len() {
        let mut seen = [false; 2];
        let mut found = None;
        for (offset, bit) in bits[index..].iter().enumerate() {
            if seen[usize::from(*bit)] {
                found = Some(offset);
                break;
            }
            seen[usize::from(*bit)] = true;
        }
        let Some(offset) = found else {
            break;
        };
        times.push((offset + 1) as f64);
        index += offset + 1;
    }
    times
}

pub fn collision(bits: &[u8]) -> f64 {
    let times = collision_times(bits);
    let (mean, deviation) = mean_and_deviation(&times);
    let lower = mean - Z_99 * deviation / (times.len() as f64).sqrt();
    match bisect_decreasing(collision_mean, lower, 0.5, 1.0 - 1e-12) {
        Some(p) => -p.log2(),
        None    => 1.0,
    }
}

// ====== markov (6.3.3) ======

pub fn markov(bits: &[u8]) -> f64 {
    let length = bits.len() as f64;
    let ones = bits.iter().filter(|bit| **bit == 1).count() as f64;
    let initial = [(length - ones) / length, ones / length];
    let mut pairs = [[0f64; 2]; 2];
    for window in bits.windows(2) {
        pairs[usize::from(window[0])][usize::from(window[1])] += 1.0;
    }
    let mut transition = [[0f64; 2]; 2];
    for (row, counts) in transition.iter_mut().zip(pairs) {
        let total = counts[0] + counts[1];
        if total > 0.0 {
            *row = [counts[0] / total, counts[1] / total];
        }
    }
    let log = |value: f64| value.log2();
    let t = |from: usize, to: usize| log(transition[from][to]);
    let candidates = [
        log(initial[0]) + 127.0 * t(0, 0),
        log(initial[0]) + 64.0 * t(0, 1) + 63.0 * t(1, 0),
        log(initial[0]) + t(0, 1) + 126.0 * t(1, 1),
        log(initial[1]) + t(1, 0) + 126.0 * t(0, 0),
        log(initial[1]) + 64.0 * t(1, 0) + 63.0 * t(0, 1),
        log(initial[1]) + 127.0 * t(1, 1),
    ];
    let most_likely = candidates.into_iter().filter(|value| !value.is_nan()).fold(f64::NEG_INFINITY, f64::max);
    (-most_likely / 128.0).min(1.0)
}

// ====== compression (6.3.4) ======

pub const COMPRESSION_BLOCK      : usize = 6;
pub const COMPRESSION_DICTIONARY : usize = 1000;

fn compression_expectation(z: f64, dictionary: usize, logarithms: &[f64], tests: usize) -> f64 {
    let miss = 1.0 - z;
    let mut prefix = 0.0;
    let mut power = 1.0;
    let mut total = 0.0;
    for (index, logarithm) in logarithms.iter().enumerate() {
        if index >= dictionary {
            total += z * z * prefix + z * power * logarithm;
        }
        prefix += logarithm * power;
        power *= miss;
    }
    total / tests as f64
}

pub fn compression_distances(bits: &[u8], dictionary: usize) -> Vec<f64> {
    let blocks: Vec<usize> = bits
        .as_chunks::<COMPRESSION_BLOCK>()
        .0
        .iter()
        .map(|chunk| chunk.iter().fold(0usize, |value, bit| value << 1 | usize::from(*bit)))
        .collect();
    let mut last = [0usize; 1 << COMPRESSION_BLOCK];
    for (index, block) in blocks.iter().take(dictionary).enumerate() {
        last[*block] = index + 1;
    }
    let mut distances = Vec::new();
    for (index, block) in blocks.iter().enumerate().skip(dictionary) {
        let position = index + 1;
        let distance = if last[*block] != 0 { position - last[*block] } else { position };
        last[*block] = position;
        distances.push(distance as f64);
    }
    distances
}

pub fn compression_with(bits: &[u8], dictionary: usize) -> f64 {
    let blocks = bits.len() / COMPRESSION_BLOCK;
    let logarithms: Vec<f64> = compression_distances(bits, dictionary).iter().map(|distance| distance.log2()).collect();
    let tests = logarithms.len();
    let count = tests as f64;
    let mean = logarithms.iter().sum::<f64>() / count;
    let squares = logarithms.iter().map(|value| value * value).sum::<f64>();
    let deviation = 0.5907 * (squares / (count - 1.0) - mean * mean).sqrt();
    let lower = mean - Z_99 * deviation / count.sqrt();
    let others = ((1 << COMPRESSION_BLOCK) - 1) as f64;
    let table: Vec<f64> = (1..=blocks).map(|t| (t as f64).log2()).collect();
    let expectation = |p: f64| {
        let q = (1.0 - p) / others;
        compression_expectation(p, dictionary, &table, tests) + others * compression_expectation(q, dictionary, &table, tests)
    };
    let floor = 2f64.powi(-(COMPRESSION_BLOCK as i32));
    match bisect_decreasing(expectation, lower, floor, 1.0) {
        Some(p) => -p.log2() / COMPRESSION_BLOCK as f64,
        None    => 1.0,
    }
}

pub fn compression(bits: &[u8]) -> f64 {
    compression_with(bits, COMPRESSION_DICTIONARY)
}
