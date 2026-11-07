#[path = "../estimators/binary.rs"]
mod binary;
#[path = "../estimators/counting.rs"]
mod counting;
#[path = "datasets.rs"]
mod datasets;
#[path = "../estimators/predictors.rs"]
mod predictors;
#[path = "../estimators/statistics.rs"]
mod statistics;
#[path = "../estimators/suffix.rs"]
mod suffix;
#[path = "../restart.rs"]
mod restart;
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

use datasets::Generator;

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
    close(counting::t_tuple_from(&suffix::tuple_profile(&TUPLES), TUPLES.len(), 3).unwrap(), 0.273, 1e-3);
}

#[test]
fn lrs_example() {
    close(counting::lrs_from(&suffix::tuple_profile(&TUPLES), TUPLES.len(), 3).unwrap(), 0.6146, 1e-3);
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
    let mut generator = Generator::new(0x5eed_90b0_2026_1107);
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
    let mut generator = Generator::new(0x7091_e5f1_2026_0001);
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

// ====== confronto con lo strumento di riferimento NIST ======

const NIST: &str = include_str!("../../vectors/nist_non_iid.tsv");

#[test]
#[cfg_attr(debug_assertions, ignore = "dataset grandi: eseguito nella build release")]
fn every_estimate_matches_the_nist_reference_tool() {
    let mut checked = 0;
    for set in datasets::all() {
        let assessment = track::assess(&set.symbols, set.bits);
        let expected: Vec<(&str, &str, f64)> = NIST
            .lines()
            .filter(|line| !line.starts_with('#'))
            .map(|line| line.split('\t').collect::<Vec<_>>())
            .filter(|fields| fields[0] == set.name)
            .map(|fields| (fields[1], fields[2], fields[3].parse().unwrap()))
            .collect();
        assert!(!expected.is_empty(), "{} senza valori NIST", set.name);
        for (track_name, estimator, value) in expected {
            let estimates = if track_name == "O" { &assessment.original } else { &assessment.bits };
            let ours = estimates.iter().find(|estimate| estimate.name == estimator).and_then(|estimate| estimate.value);
            let ours = ours.unwrap_or_else(|| panic!("{} {track_name} {estimator} mancante", set.name));
            assert!((ours - value).abs() <= 1.5e-6, "{} {track_name} {estimator}: NIST {value}, nostro {ours}", set.name);
            checked += 1;
        }
    }
    assert_eq!(checked, 105);
}

// ====== test di riavvio (3.1.4) ======

fn matrix_of(data: &[u8], restarts: usize, samples: usize) -> restart::Matrix<'_> {
    restart::Matrix { data, restarts, samples }
}

#[test]
fn column_dataset_concatenates_columns() {
    let data = [1, 2, 3, 4, 5, 6];
    assert_eq!(restart::column_dataset(&matrix_of(&data, 2, 3)), [1, 4, 2, 5, 3, 6]);
}

#[test]
fn binomial_tail_matches_exact_sums() {
    let choose = |n: u128, k: u128| (1..=k).fold(1u128, |value, i| value * (n - k + i) / i);
    for (trials, p) in [(20usize, 0.5f64), (30, 0.1), (25, 0.9)] {
        for at_least in 0..=trials + 1 {
            let exact: f64 = (at_least..=trials)
                .map(|j| choose(trials as u128, j as u128) as f64 * p.powi(j as i32) * (1.0 - p).powi((trials - j) as i32))
                .sum();
            close(restart::binomial_tail(trials, at_least, p), exact, 1e-12);
        }
    }
    assert_eq!(restart::binomial_tail(1000, 1000, 1.0), 1.0);
    assert_eq!(restart::binomial_tail(1000, 1001, 1.0), 0.0);
}

#[test]
fn sanity_check_counts_rows_and_columns() {
    let data = [
        7, 7, 7, 1, 2,
        3, 7, 4, 5, 6,
        8, 7, 9, 0, 1,
        2, 7, 3, 4, 5,
    ];
    let (rows, columns, _) = restart::sanity_check(&matrix_of(&data, 4, 5), 1.0);
    assert_eq!((rows, columns), (3, 4));
}

#[test]
fn healthy_restarts_are_validated() {
    let mut generator = Generator::new(0x0e57_a127_2026_1107);
    let data: Vec<u8> = (0..200 * 200).map(|_| (generator.next() >> 40) as u8).collect();
    let report = restart::restart_test(&matrix_of(&data, 200, 200), 8, 7.0);
    assert!(report.sanity, "{}", report.probability);
    let value = report.assessment.expect("validato");
    assert!((3.5..=7.0).contains(&value), "{value}");
    assert_eq!((report.rows.len(), report.columns.len()), (7, 7));
}

#[test]
fn repeated_restarts_fail_the_sanity_check() {
    let mut generator = Generator::new(0x5a9e_0001_2026_1107);
    let row: Vec<u8> = (0..200).map(|_| (generator.next() >> 40) as u8).collect();
    let data = row.repeat(200);
    let report = restart::restart_test(&matrix_of(&data, 200, 200), 8, 7.0);
    assert!(!report.sanity);
    assert_eq!(report.most_in_columns, 200);
    assert!(report.most_in_rows < 10);
    assert_eq!(restart::RESTARTS * restart::RESTART_SAMPLES, 1_000_000);
    assert!(report.rows.is_empty() && report.assessment.is_none());
}

#[test]
fn overclaimed_entropy_fails_the_sanity_check() {
    let mut generator = Generator::new(0x0c1a_1111_2026_1107);
    let data: Vec<u8> = (0..200 * 200).map(|_| (generator.next() >> 60) as u8).collect();
    assert!(!restart::restart_test(&matrix_of(&data, 200, 200), 8, 8.0).sanity);
    assert!(restart::restart_test(&matrix_of(&data, 200, 200), 8, 3.5).sanity);
}

#[test]
fn predictable_restarts_fail_the_half_entropy_rule() {
    let data: Vec<u8> = (0..200 * 200).map(|index| ((index / 200 + index % 200) % 256) as u8).collect();
    let report = restart::restart_test(&matrix_of(&data, 200, 200), 8, 7.0);
    assert!(report.sanity, "{}", report.probability);
    assert!(report.assessment.is_none());
    assert!(track::minimum(&report.columns) < 3.5);
}

#[test]
fn half_entropy_rule_uses_exactly_half_of_the_initial_estimate() {
    let noisy_diagonal = |spread: u64| {
        let mut generator = Generator::new(0x3a1c_2026);
        let mut data = Vec::with_capacity(200 * 200);
        for row in 0..200u64 {
            for column in 0..200u64 {
                data.push(((row + column + generator.next() % spread) % 256) as u8);
            }
        }
        data
    };
    let narrow = noisy_diagonal(4);
    let report = restart::restart_test(&matrix_of(&narrow, 200, 200), 8, 6.0);
    let worst = track::minimum(&report.rows).min(track::minimum(&report.columns));
    assert!(report.sanity && (1.5..3.0).contains(&worst), "{worst}");
    assert!(report.assessment.is_none());
    let wide = noisy_diagonal(8);
    let report = restart::restart_test(&matrix_of(&wide, 200, 200), 8, 6.0);
    let worst = track::minimum(&report.rows).min(track::minimum(&report.columns));
    assert!(worst >= 3.0, "{worst}");
    assert_eq!(report.assessment, Some(worst));
}
