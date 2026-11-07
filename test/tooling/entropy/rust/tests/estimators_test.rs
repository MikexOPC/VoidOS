#[path = "../binary.rs"]
mod binary;
#[path = "../counting.rs"]
mod counting;
#[path = "../predictors.rs"]
mod predictors;
#[path = "../statistics.rs"]
mod statistics;
#[path = "../suffix.rs"]
mod suffix;
#[path = "../track.rs"]
mod track;

// ====== supporto ======

fn close(actual: f64, expected: f64, tolerance: f64) {
    assert!((actual - expected).abs() <= tolerance, "atteso {expected}, ottenuto {actual}");
}

const BITS_40: [u8; 40] = [
    1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0,
    1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0,
];

const TUPLES: [u8; 21] = [2, 2, 0, 1, 0, 2, 0, 1, 2, 1, 2, 0, 1, 2, 1, 0, 0, 1, 0, 0, 0];

struct Generator(u64);

impl Generator {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
}

// ====== esempi ufficiali 6.3.1 - 6.3.4 ======

#[test]
fn most_common_value_example() {
    let symbols = [0, 1, 1, 2, 0, 1, 2, 2, 0, 1, 0, 1, 1, 0, 2, 2, 1, 0, 2, 1];
    close(statistics::upper_bound(0.4, 20), 0.6895, 1e-4);
    close(counting::most_common_value(&symbols), 0.5363, 1e-3);
}

#[test]
fn collision_example() {
    let times = binary::collision_times(&BITS_40);
    assert_eq!(times, [3., 3., 3., 3., 3., 2., 3., 2., 2., 3., 3., 3., 3., 2.]);
    close(binary::collision(&BITS_40), 0.4483, 1e-3);
}

#[test]
fn markov_example() {
    close(binary::markov(&BITS_40), 0.761, 1e-3);
}

#[test]
fn compression_example() {
    let mut bits = BITS_40.to_vec();
    bits.extend([1, 1, 1, 0, 0, 0, 1, 1]);
    assert_eq!(binary::compression_distances(&bits, 4), [5., 6., 7., 7.]);
    close(binary::compression_with(&bits, 4), 0.1345, 1e-3);
}

// ====== esempi ufficiali 6.3.5 - 6.3.6 ======

#[test]
fn t_tuple_example() {
    close(counting::t_tuple_with(&TUPLES, 3).unwrap(), 0.273, 1e-3);
}

#[test]
fn lrs_example() {
    close(counting::lrs_with(&TUPLES, 3).unwrap(), 0.6146, 1e-3);
}

// ====== esempi ufficiali 6.3.7 - 6.3.10 ======

#[test]
fn multi_mcw_example() {
    let symbols = [1, 2, 1, 0, 2, 1, 1, 2, 2, 0, 0, 0];
    close(predictors::multi_mcw_with(&symbols, [3, 5, 7, 9]), 0.3908, 1e-3);
}

#[test]
fn lag_example() {
    let symbols = [2, 1, 3, 2, 1, 3, 1, 3, 1, 2];
    close(predictors::lag_with(&symbols, 3), 0.735, 1e-3);
}

#[test]
fn multi_mmc_example() {
    let symbols = [2, 1, 3, 2, 1, 3, 1, 3, 1];
    close(predictors::multi_mmc_with(&symbols, 3), 0.0755, 1e-3);
}

#[test]
fn lz78y_example() {
    let symbols = [2, 1, 3, 2, 1, 3, 1, 3, 1, 2, 1, 3, 2];
    close(predictors::lz78y_with(&symbols, 4), 0.0191, 1e-3);
}

// ====== prestazioni locali e globali ======

#[test]
fn local_performance_matches_table_three() {
    for (run, expected) in [(2, 0.0001), (4, 0.0100), (10, 0.1614), (20, 0.4088), (40, 0.6477), (200, 0.9239), (500, 0.9707)] {
        close(statistics::local_performance(run, 1_000_000), expected, 1e-4);
    }
}

#[test]
fn predictor_examples_have_the_stated_intermediate_values() {
    close(statistics::global_performance(3, 9), 0.7627, 1e-4);
    close(statistics::local_performance(2, 9), 0.036, 1e-3);
    close(statistics::global_performance(2, 9), 0.6008, 1e-4);
    close(statistics::local_performance(3, 9), 0.1167, 1e-3);
    close(statistics::global_performance(0, 100), 1.0 - 0.01f64.powf(0.01), 1e-12);
}

// ====== proprieta su dati sintetici ======

#[test]
fn constant_data_has_almost_no_entropy() {
    let symbols = vec![7u8; 20_000];
    let assessment = track::assess(&symbols, 8);
    assert!(assessment.per_sample < 0.01, "{}", assessment.per_sample);
    assert_eq!(assessment.original.len(), 7);
    assert_eq!(assessment.bits.len(), 10);
    assert!(track::assess(&[0, 1, 1, 0, 1, 0, 0, 1].repeat(500), 1).bits.is_empty());
}

#[test]
fn uniform_bytes_score_high_and_bounded() {
    let mut generator = Generator(0x5eed_90b0_2026_1107);
    let symbols: Vec<u8> = (0..20_000).map(|_| (generator.next() >> 24) as u8).collect();
    let original = track::non_iid(&symbols, false);
    for estimate in &original {
        if let Some(value) = estimate.value {
            assert!(value > 5.0 && value <= 8.0, "{} = {value}", estimate.name);
        }
    }
}

#[test]
fn periodic_data_is_caught_by_predictors() {
    let symbols: Vec<u8> = (0..20_000).map(|index| (index % 17) as u8).collect();
    let assessment = track::assess(&symbols, 8);
    assert!(assessment.per_sample < 0.1, "{}", assessment.per_sample);
}

#[test]
fn bitstring_is_most_significant_bit_first_and_limited() {
    assert_eq!(track::bitstring(&[0b100, 0b011], 3), [1, 0, 0, 0, 1, 1]);
    assert_eq!(track::bitstring(&vec![0xff; 200_000], 8).len(), track::BITSTRING_LIMIT);
}

// ====== profilo delle tuple contro conteggio diretto ======

fn naive_counts(symbols: &[u8], width: usize) -> Vec<usize> {
    let mut counts = std::collections::HashMap::new();
    for window in symbols.windows(width) {
        *counts.entry(window).or_insert(0usize) += 1;
    }
    counts.into_values().collect()
}

#[test]
fn tuple_profile_matches_direct_counting() {
    let mut generator = Generator(0x7091_e5f1_2026_0001);
    for round in 0..300 {
        let length = 2 + (generator.next() % 120) as usize;
        let alphabet = 1 + (round % 4) as u64;
        let symbols: Vec<u8> = (0..length).map(|_| (generator.next() % alphabet) as u8).collect();
        let profile = suffix::tuple_profile(&symbols);
        for width in 1..profile.most.len().min(length + 1) {
            let counts = naive_counts(&symbols, width);
            assert_eq!(profile.most[width], counts.iter().copied().max().unwrap_or(0), "most {width} di {symbols:?}");
            let pairs: f64 = counts.iter().map(|count| (count * count.saturating_sub(1) / 2) as f64).sum();
            assert_eq!(profile.pairs[width], pairs, "pairs {width}");
        }
        assert!(naive_counts(&symbols, profile.most.len() - 1).iter().all(|count| *count < 2));
    }
}

// ====== casi che decidono rami non coperti dagli esempi ======

#[test]
fn markov_all_zero_chain_can_be_the_most_likely() {
    let mut bits = vec![0u8; 990];
    bits.extend([1, 0].repeat(5));
    let zeros = bits.iter().filter(|bit| **bit == 0).count() as f64;
    let (mut stay, mut leave) = (0.0f64, 0.0f64);
    for pair in bits.windows(2).filter(|pair| pair[0] == 0) {
        if pair[1] == 0 { stay += 1.0 } else { leave += 1.0 }
    }
    let expected = -((zeros / bits.len() as f64).log2() + 127.0 * (stay / (stay + leave)).log2()) / 128.0;
    close(binary::markov(&bits), expected, 1e-9);
}

#[test]
fn multi_mcw_ties_pick_the_most_recent_value() {
    let symbols: Vec<u8> = (0..60).map(|index| (index % 3) as u8).collect();
    close(predictors::multi_mcw_with(&symbols, [3, 5, 7, 9]), 3f64.log2(), 1e-9);
}
