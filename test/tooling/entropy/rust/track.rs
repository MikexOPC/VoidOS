use crate::binary::{collision, compression, markov};
use crate::counting::{most_common_value, tuples};
use crate::predictors::{lag, lz78y, multi_mcw, multi_mmc};

// ====== stime per un insieme di dati ======

pub const BITSTRING_LIMIT: usize = 1_000_000;

#[derive(Debug)]
pub struct Estimate {
    pub name  : &'static str,
    pub value : Option<f64>,
}

pub fn non_iid(symbols: &[u8], binary: bool) -> Vec<Estimate> {
    let mut estimates = vec![Estimate { name: "most common value", value: Some(most_common_value(symbols)) }];
    if binary {
        estimates.push(Estimate { name: "collision", value: Some(collision(symbols)) });
        estimates.push(Estimate { name: "markov", value: Some(markov(symbols)) });
        estimates.push(Estimate { name: "compression", value: Some(compression(symbols)) });
    }
    let (t_tuple, lrs) = tuples(symbols);
    estimates.push(Estimate { name: "t-tuple", value: t_tuple });
    estimates.push(Estimate { name: "lrs", value: lrs });
    estimates.push(Estimate { name: "multi mcw", value: Some(multi_mcw(symbols)) });
    estimates.push(Estimate { name: "lag", value: Some(lag(symbols)) });
    estimates.push(Estimate { name: "multi mmc", value: Some(multi_mmc(symbols)) });
    estimates.push(Estimate { name: "lz78y", value: Some(lz78y(symbols)) });
    estimates
}

pub fn minimum(estimates: &[Estimate]) -> f64 {
    estimates.iter().filter_map(|estimate| estimate.value).fold(f64::INFINITY, f64::min)
}

// ====== bitstring (3.1.3) ======

pub fn bitstring(symbols: &[u8], bits: u32) -> Vec<u8> {
    symbols
        .iter()
        .flat_map(|symbol| (0..bits).rev().map(move |shift| symbol >> shift & 1))
        .take(BITSTRING_LIMIT)
        .collect()
}

// ====== valutazione completa ======

pub struct Assessment {
    pub original   : Vec<Estimate>,
    pub bits       : Vec<Estimate>,
    pub per_sample : f64,
}

pub fn assess(symbols: &[u8], bits: u32) -> Assessment {
    if bits == 1 {
        let original = non_iid(symbols, true);
        let per_sample = minimum(&original);
        return Assessment { original, bits: Vec::new(), per_sample };
    }
    let original = non_iid(symbols, false);
    let expanded = non_iid(&bitstring(symbols, bits), true);
    let per_sample = minimum(&original).min(f64::from(bits) * minimum(&expanded));
    Assessment { original, bits: expanded, per_sample }
}
