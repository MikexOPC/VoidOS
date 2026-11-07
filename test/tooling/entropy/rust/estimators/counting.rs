use crate::statistics::upper_bound;
use crate::suffix::{TupleProfile, tuple_profile};

// ====== most common value (6.3.1) ======

pub fn most_common_value(symbols: &[u8]) -> f64 {
    let mut counts = [0usize; 256];
    for symbol in symbols {
        counts[usize::from(*symbol)] += 1;
    }
    let most = *counts.iter().max().unwrap_or(&0) as f64;
    -upper_bound(most / symbols.len() as f64, symbols.len()).log2()
}

// ====== t-tuple (6.3.5) ======

pub const TUPLE_CUTOFF: usize = 35;

pub fn t_tuple_from(profile: &TupleProfile, length: usize, cutoff: usize) -> Option<f64> {
    let mut best = None::<f64>;
    for (size, most) in profile.most.iter().enumerate().skip(1) {
        if *most < cutoff {
            break;
        }
        let proportion = *most as f64 / (length - size + 1) as f64;
        let per_symbol = proportion.powf(1.0 / size as f64);
        best = Some(best.map_or(per_symbol, |value| value.max(per_symbol)));
    }
    best.map(|proportion| -upper_bound(proportion, length).log2())
}

// ====== longest repeated substring (6.3.6) ======

fn pairs(count: f64) -> f64 {
    count * (count - 1.0) / 2.0
}

pub fn lrs_from(profile: &TupleProfile, length: usize, cutoff: usize) -> Option<f64> {
    let start = profile.most.iter().enumerate().skip(1).find(|(_, most)| **most < cutoff).map(|(size, _)| size)?;
    let longest = profile.most.iter().rposition(|most| *most >= 2)?;
    let mut best = None::<f64>;
    for size in start..=longest {
        let probability = profile.pairs[size] / pairs((length - size + 1) as f64);
        let per_symbol = probability.powf(1.0 / size as f64);
        best = Some(best.map_or(per_symbol, |value| value.max(per_symbol)));
    }
    best.map(|proportion| -upper_bound(proportion, length).log2())
}

// ====== tuples ======

pub fn tuples(symbols: &[u8]) -> (Option<f64>, Option<f64>) {
    let profile = tuple_profile(symbols);
    (t_tuple_from(&profile, symbols.len(), TUPLE_CUTOFF), lrs_from(&profile, symbols.len(), TUPLE_CUTOFF))
}
