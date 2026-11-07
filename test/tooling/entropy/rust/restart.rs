use crate::track::{Estimate, minimum, non_iid};

// ====== costanti SP 800-90B 3.1.4 ======

pub const RESTARTS        : usize = 1000;
pub const RESTART_SAMPLES : usize = 1000;
pub const SANITY_ALPHA    : f64   = 0.000_005;

pub struct Matrix<'a> {
    pub data     : &'a [u8],
    pub restarts : usize,
    pub samples  : usize,
}

pub struct RestartReport {
    pub most_in_rows    : usize,
    pub most_in_columns : usize,
    pub probability     : f64,
    pub sanity          : bool,
    pub rows            : Vec<Estimate>,
    pub columns         : Vec<Estimate>,
    pub assessment      : Option<f64>,
}

// ====== dataset per righe e per colonne (3.1.4.1) ======

pub fn column_dataset(matrix: &Matrix) -> Vec<u8> {
    (0..matrix.samples)
        .flat_map(|column| (0..matrix.restarts).map(move |row| matrix.data[row * matrix.samples + column]))
        .collect()
}

fn largest_count(group: impl Iterator<Item = u8>) -> usize {
    let mut counts = [0usize; 256];
    group.fold(0, |best, symbol| {
        counts[usize::from(symbol)] += 1;
        best.max(counts[usize::from(symbol)])
    })
}

// ====== controllo di sanita (3.1.4.3) ======

pub fn binomial_tail(trials: usize, at_least: usize, p: f64) -> f64 {
    if at_least == 0 {
        return 1.0;
    }
    if p >= 1.0 {
        return f64::from(u8::from(at_least <= trials));
    }
    let mut logarithm = 0.0;
    let mut total = 0.0;
    for successes in 0..=trials {
        if successes > 0 {
            logarithm += ((trials - successes + 1) as f64 / successes as f64).ln();
        }
        if successes >= at_least {
            total += (logarithm + successes as f64 * p.ln() + (trials - successes) as f64 * (1.0 - p).ln()).exp();
        }
    }
    total
}

pub fn sanity_check(matrix: &Matrix, initial: f64) -> (usize, usize, f64) {
    let rows = (0..matrix.restarts)
        .map(|row| largest_count(matrix.data[row * matrix.samples..(row + 1) * matrix.samples].iter().copied()))
        .max()
        .unwrap_or(0);
    let columns = (0..matrix.samples)
        .map(|column| largest_count((0..matrix.restarts).map(|row| matrix.data[row * matrix.samples + column])))
        .max()
        .unwrap_or(0);
    let p = 2f64.powf(-initial);
    let probability = binomial_tail(matrix.samples, rows, p).min(binomial_tail(matrix.restarts, columns, p));
    (rows, columns, probability)
}

// ====== validazione (3.1.4.2) ======

pub fn restart_test(matrix: &Matrix, bits: u32, initial: f64) -> RestartReport {
    let (most_in_rows, most_in_columns, probability) = sanity_check(matrix, initial);
    let sanity = probability >= SANITY_ALPHA;
    let mut report = RestartReport {
        most_in_rows,
        most_in_columns,
        probability,
        sanity,
        rows       : Vec::new(),
        columns    : Vec::new(),
        assessment : None,
    };
    if !sanity {
        return report;
    }
    report.rows = non_iid(matrix.data, bits == 1);
    report.columns = non_iid(&column_dataset(matrix), bits == 1);
    let worst = minimum(&report.rows).min(minimum(&report.columns));
    if worst >= initial / 2.0 {
        report.assessment = Some(worst.min(initial));
    }
    report
}
