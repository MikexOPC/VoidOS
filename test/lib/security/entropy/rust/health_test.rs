#[path = "../../../../../lib/security/entropy/rust/health.rs"]
mod health;

use health::*;

// ====== supporto ======

const NON_BINARY: HealthConfig = HealthConfig {
    min_entropy_millibits   : 8000,
    false_positive_exponent : 20,
    binary                  : false,
    proportion_cutoff       : 13,
};

const BINARY: HealthConfig = HealthConfig {
    min_entropy_millibits   : 1000,
    false_positive_exponent : 20,
    binary                  : true,
    proportion_cutoff       : 589,
};

struct Source(u64);

impl Source {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
}

fn monitor(config: HealthConfig) -> HealthMonitor {
    HealthMonitor::new(config).expect("valid configuration")
}

fn feed_all(health: &mut HealthMonitor, samples: impl IntoIterator<Item = u64>) -> HealthStatus {
    let mut last = health.status();
    for sample in samples {
        last = health.feed(sample);
    }
    last
}

fn distinct(start: u64) -> impl Iterator<Item = u64> {
    (start..).map(|value| value | 1 << 63)
}

// ====== oracolo CRITBINOM su Table 2 di SP 800-90B ======

fn ln_choose(n: u32, k: u32) -> f64 {
    (1..=k).map(|i| (f64::from(n - k + i) / f64::from(i)).ln()).sum()
}

fn proportion_cutoff_oracle(window: u32, entropy: f64, exponent: i32) -> u32 {
    let p = 2f64.powf(-entropy);
    let alpha = 2f64.powi(-exponent);
    let mut tail = 0.0;
    for k in (0..=window).rev() {
        let mass = (ln_choose(window, k) + f64::from(k) * p.ln() + f64::from(window - k) * (1.0 - p).ln()).exp();
        if tail + mass > alpha {
            return k + 1;
        }
        tail += mass;
    }
    1
}

#[test]
fn proportion_oracle_reproduces_table_two() {
    for (entropy, cutoff) in [(0.2, 941), (0.4, 840), (0.6, 748), (0.8, 664), (1.0, 589)] {
        assert_eq!(proportion_cutoff_oracle(BINARY_WINDOW, entropy, 20), cutoff);
    }
    for (entropy, cutoff) in [(0.5, 410), (1.0, 311), (2.0, 177), (4.0, 62), (8.0, 13)] {
        assert_eq!(proportion_cutoff_oracle(NON_BINARY_WINDOW, entropy, 20), cutoff);
    }
}

#[test]
fn table_two_profiles_are_accepted() {
    for (millibits, cutoff) in [(200, 941), (400, 840), (600, 748), (800, 664), (1000, 589)] {
        let config = HealthConfig { min_entropy_millibits: millibits, proportion_cutoff: cutoff, ..BINARY };
        assert_eq!(monitor(config).window(), 1024);
    }
    for (millibits, cutoff) in [(500, 410), (1000, 311), (2000, 177), (4000, 62), (8000, 13)] {
        let config = HealthConfig { min_entropy_millibits: millibits, proportion_cutoff: cutoff, ..NON_BINARY };
        assert_eq!(monitor(config).window(), 512);
    }
}

// ====== cutoff del repetition count test ======

#[test]
fn repetition_cutoff_follows_the_standard_formula() {
    assert_eq!(repetition_cutoff(2000, 20), Some(11));
    assert_eq!(repetition_cutoff(8000, 40), Some(6));
    assert_eq!(repetition_cutoff(1000, 20), Some(21));
    assert_eq!(repetition_cutoff(64_000, 20), Some(2));
    assert_eq!(repetition_cutoff(1, 64), Some(64_001));
    for millibits in 1..=MAXIMUM_ENTROPY_MILLIBITS {
        for exponent in [20, 33, 40, 64] {
            let expected = 1 + (f64::from(exponent) * 1000.0 / f64::from(millibits)).ceil() as u32;
            assert_eq!(repetition_cutoff(millibits, exponent), Some(expected));
        }
    }
}

#[test]
fn invalid_configurations_are_rejected() {
    assert_eq!(repetition_cutoff(0, 20), None);
    assert_eq!(repetition_cutoff(64_001, 20), None);
    assert_eq!(repetition_cutoff(1000, 19), None);
    assert_eq!(repetition_cutoff(1000, 65), None);
    assert_eq!(repetition_cutoff(u32::MAX, u32::MAX), None);
    for config in [
        HealthConfig { proportion_cutoff: 0, ..NON_BINARY },
        HealthConfig { proportion_cutoff: 1, ..NON_BINARY },
        HealthConfig { proportion_cutoff: 513, ..NON_BINARY },
        HealthConfig { proportion_cutoff: 1025, ..BINARY },
        HealthConfig { min_entropy_millibits: 1001, ..BINARY },
        HealthConfig { min_entropy_millibits: 0, ..NON_BINARY },
        HealthConfig { false_positive_exponent: 10, ..NON_BINARY },
    ] {
        assert!(HealthMonitor::new(config).is_none());
    }
    assert!(HealthMonitor::new(HealthConfig { proportion_cutoff: 512, ..NON_BINARY }).is_some());
    assert!(HealthMonitor::new(HealthConfig { proportion_cutoff: 2, ..NON_BINARY }).is_some());
}

// ====== avvio ======

#[test]
fn startup_needs_exactly_1024_good_samples() {
    let mut health = monitor(NON_BINARY);
    let mut samples = distinct(1);
    for _ in 1..STARTUP_SAMPLES {
        assert_eq!(health.feed(samples.next().unwrap()), HealthStatus::Startup);
    }
    assert_eq!(health.feed(samples.next().unwrap()), HealthStatus::Healthy);
    assert_eq!(feed_all(&mut health, samples.take(100_000)), HealthStatus::Healthy);
}

#[test]
fn good_random_sources_stay_healthy() {
    let mut source = Source(0x2026_1106_5eed_0001);
    let mut health = monitor(NON_BINARY);
    assert_eq!(feed_all(&mut health, (0..200_000).map(|_| source.next())), HealthStatus::Healthy);
    let mut bits = monitor(BINARY);
    assert_eq!(feed_all(&mut bits, (0..50_000).map(|_| source.next() >> 63)), HealthStatus::Healthy);
}

// ====== guasti ======

#[test]
fn stuck_source_fails_at_the_repetition_cutoff() {
    let mut health = monitor(NON_BINARY);
    let cutoff = health.repetition_cutoff();
    assert_eq!(cutoff, 4);
    for _ in 1..cutoff {
        assert_eq!(health.feed(7), HealthStatus::Startup);
    }
    assert_eq!(health.feed(7), HealthStatus::RepetitionFailure);
    let mut healthy = monitor(NON_BINARY);
    feed_all(&mut healthy, distinct(1).take(2048));
    for _ in 1..cutoff {
        assert_eq!(healthy.feed(0), HealthStatus::Healthy);
    }
    assert_eq!(healthy.feed(0), HealthStatus::RepetitionFailure);
}

#[test]
fn repetition_count_restarts_on_a_new_value() {
    let mut health = monitor(NON_BINARY);
    let pattern = [5, 5, 5, 6, 6, 6, 5, 5, 5];
    assert_eq!(feed_all(&mut health, pattern), HealthStatus::Startup);
}

#[test]
fn proportion_fails_when_a_value_reaches_the_cutoff_in_one_window() {
    let mut health = monitor(NON_BINARY);
    let mut filler = distinct(1);
    let cutoff = health.proportion_cutoff();
    for _ in 1..cutoff {
        assert_eq!(health.feed(42), HealthStatus::Startup);
        assert_eq!(health.feed(filler.next().unwrap()), HealthStatus::Startup);
    }
    assert_eq!(health.feed(42), HealthStatus::ProportionFailure);
}

#[test]
fn proportion_count_resets_at_the_window_boundary() {
    let mut health = monitor(NON_BINARY);
    let mut filler = distinct(1);
    let window = health.window();
    let cutoff = health.proportion_cutoff();
    let mut seen = 0;
    for _ in 1..cutoff {
        health.feed(42);
        health.feed(filler.next().unwrap());
        seen += 2;
    }
    while seen < window {
        assert_ne!(health.feed(filler.next().unwrap()), HealthStatus::ProportionFailure);
        seen += 1;
    }
    for _ in 1..cutoff {
        health.feed(42);
        assert_eq!(health.feed(filler.next().unwrap()), HealthStatus::Startup);
    }
    assert_eq!(health.feed(filler.next().unwrap()), HealthStatus::Startup);
}

#[test]
fn only_the_first_sample_of_the_window_is_counted() {
    let mut health = monitor(NON_BINARY);
    let mut filler = distinct(1);
    health.feed(1);
    for _ in 0..40 {
        health.feed(42);
        assert_ne!(health.feed(filler.next().unwrap()), HealthStatus::ProportionFailure);
    }
}

#[test]
fn periodic_and_biased_sources_are_caught() {
    let mut periodic = monitor(NON_BINARY);
    assert_eq!(feed_all(&mut periodic, (0..1024).map(|i| i % 2)), HealthStatus::ProportionFailure);
    let mut cycle = monitor(NON_BINARY);
    assert_eq!(feed_all(&mut cycle, (0..1024).map(|i| i % 30)), HealthStatus::ProportionFailure);
    let mut source = Source(0x0bad_5eed_0000_0001);
    let mut biased = monitor(BINARY);
    let status = feed_all(&mut biased, (0..20_000).map(|_| u64::from(!source.next().is_multiple_of(8))));
    assert!(matches!(status, HealthStatus::ProportionFailure | HealthStatus::RepetitionFailure));
}

#[test]
fn binary_sources_reject_non_binary_samples() {
    let mut health = monitor(BINARY);
    assert_eq!(health.feed(1), HealthStatus::Startup);
    assert_eq!(health.feed(2), HealthStatus::InvalidSample);
    assert_eq!(health.feed(0), HealthStatus::InvalidSample);
}

// ====== guasto persistente e riavvio ======

#[test]
fn failures_are_sticky_until_restart() {
    let mut health = monitor(NON_BINARY);
    feed_all(&mut health, [9, 9, 9, 9]);
    assert_eq!(health.status(), HealthStatus::RepetitionFailure);
    assert_eq!(feed_all(&mut health, distinct(1).take(5000)), HealthStatus::RepetitionFailure);
    health.restart();
    assert_eq!(health.status(), HealthStatus::Startup);
    assert_eq!(health.feed(9), HealthStatus::Startup);
    assert_eq!(feed_all(&mut health, distinct(1).take(1023)), HealthStatus::Healthy);
}

#[test]
fn first_failure_kind_is_kept() {
    let mut health = monitor(BINARY);
    feed_all(&mut health, [1; 21]);
    assert_eq!(health.status(), HealthStatus::RepetitionFailure);
    assert_eq!(health.feed(2), HealthStatus::RepetitionFailure);
    let mut proportion = monitor(NON_BINARY);
    feed_all(&mut proportion, (0..26).map(|i| if i % 2 == 0 { 42 } else { 100 + i }));
    assert_eq!(proportion.status(), HealthStatus::ProportionFailure);
    assert_eq!(feed_all(&mut proportion, [7, 7, 7, 7]), HealthStatus::ProportionFailure);
}

#[test]
fn restart_clears_partial_counts() {
    let mut health = monitor(NON_BINARY);
    feed_all(&mut health, [3, 3, 3]);
    health.restart();
    assert_eq!(feed_all(&mut health, [3, 3, 3]), HealthStatus::Startup);
    assert_eq!(health.feed(3), HealthStatus::RepetitionFailure);
}
