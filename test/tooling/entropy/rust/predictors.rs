use crate::statistics::predictor_entropy;
use std::collections::HashMap;

// ====== scoreboard ======

fn update_scoreboard(guesses: &[Option<u8>], actual: u8, scoreboard: &mut [usize], winner: &mut usize) {
    for (index, guess) in guesses.iter().enumerate() {
        if *guess == Some(actual) {
            scoreboard[index] += 1;
            if scoreboard[index] >= scoreboard[*winner] {
                *winner = index;
            }
        }
    }
}

fn alphabet(symbols: &[u8]) -> usize {
    let mut present = [false; 256];
    for symbol in symbols {
        present[usize::from(*symbol)] = true;
    }
    present.iter().filter(|seen| **seen).count()
}

// ====== multi most common in window (6.3.7) ======

pub const MCW_WINDOWS: [usize; 4] = [63, 255, 1023, 4095];

pub fn multi_mcw_with(symbols: &[u8], windows: [usize; 4]) -> f64 {
    let length = symbols.len();
    let mut counts = [[0usize; 256]; 4];
    let mut last_seen = [0usize; 256];
    let mut scoreboard = [0usize; 4];
    let mut winner = 0;
    let mut correct = Vec::with_capacity(length - windows[0]);
    for i in 1..=length {
        if i > 1 {
            let entering = usize::from(symbols[i - 2]);
            last_seen[entering] = i - 1;
            for (window, count) in windows.iter().zip(counts.iter_mut()) {
                count[entering] += 1;
                if i - 1 > *window {
                    count[usize::from(symbols[i - 2 - window])] -= 1;
                }
            }
        }
        if i <= windows[0] {
            continue;
        }
        let mut frequent = [None; 4];
        for (slot, (window, count)) in frequent.iter_mut().zip(windows.iter().zip(counts.iter())) {
            if i > *window {
                let best = (0..256usize).filter(|symbol| count[*symbol] > 0).max_by_key(|symbol| (count[*symbol], last_seen[*symbol]));
                *slot = best.map(|symbol| symbol as u8);
            }
        }
        let actual = symbols[i - 1];
        correct.push(frequent[winner] == Some(actual));
        update_scoreboard(&frequent, actual, &mut scoreboard, &mut winner);
    }
    predictor_entropy(&correct, alphabet(symbols))
}

pub fn multi_mcw(symbols: &[u8]) -> f64 {
    multi_mcw_with(symbols, MCW_WINDOWS)
}

// ====== lag (6.3.8) ======

pub const LAG_DEPTH: usize = 128;

pub fn lag_with(symbols: &[u8], depth: usize) -> f64 {
    let mut scoreboard = vec![0usize; depth];
    let mut lags = vec![None; depth];
    let mut winner = 0;
    let mut correct = Vec::with_capacity(symbols.len() - 1);
    for i in 2..=symbols.len() {
        for (d, lag) in lags.iter_mut().enumerate() {
            *lag = if d + 1 < i { Some(symbols[i - d - 2]) } else { None };
        }
        let actual = symbols[i - 1];
        correct.push(lags[winner] == Some(actual));
        update_scoreboard(&lags, actual, &mut scoreboard, &mut winner);
    }
    predictor_entropy(&correct, alphabet(symbols))
}

pub fn lag(symbols: &[u8]) -> f64 {
    lag_with(symbols, LAG_DEPTH)
}

// ====== multi markov model with counting (6.3.9) ======

pub const MMC_DEPTH   : usize = 16;
pub const MMC_ENTRIES : usize = 100_000;

fn context_key(context: &[u8]) -> u128 {
    context.iter().fold(0u128, |key, symbol| key << 8 | u128::from(*symbol))
}

fn most_frequent(followers: &[(u8, usize)]) -> Option<u8> {
    followers.iter().filter(|(_, count)| *count > 0).max_by_key(|(symbol, count)| (*count, *symbol)).map(|(symbol, _)| *symbol)
}

pub fn multi_mmc_with(symbols: &[u8], depth: usize) -> f64 {
    let mut models: Vec<HashMap<u128, Vec<(u8, usize)>>> = vec![HashMap::new(); depth];
    let mut entries = vec![0usize; depth];
    let mut scoreboard = vec![0usize; depth];
    let mut winner = 0;
    let mut correct = Vec::with_capacity(symbols.len() - 2);
    for i in 3..=symbols.len() {
        for d in 1..=depth {
            if d < i - 1 {
                let key = context_key(&symbols[i - d - 2..i - 2]);
                let next = symbols[i - 2];
                let model = &mut models[d - 1];
                let existing = model.get_mut(&key).and_then(|followers| followers.iter_mut().find(|(symbol, _)| *symbol == next));
                if let Some((_, count)) = existing {
                    *count += 1;
                } else if entries[d - 1] < MMC_ENTRIES {
                    model.entry(key).or_default().push((next, 1));
                    entries[d - 1] += 1;
                }
            }
        }
        let mut guesses = vec![None; depth];
        for (d, guess) in guesses.iter_mut().enumerate() {
            if d + 1 < i {
                let key = context_key(&symbols[i - d - 2..i - 1]);
                *guess = models[d].get(&key).and_then(|followers| most_frequent(followers));
            }
        }
        let actual = symbols[i - 1];
        correct.push(guesses[winner] == Some(actual));
        update_scoreboard(&guesses, actual, &mut scoreboard, &mut winner);
    }
    predictor_entropy(&correct, alphabet(symbols))
}

pub fn multi_mmc(symbols: &[u8]) -> f64 {
    multi_mmc_with(symbols, MMC_DEPTH)
}

// ====== lz78y (6.3.10) ======

pub const LZ78Y_DEPTH      : usize = 16;
pub const LZ78Y_DICTIONARY : usize = 65_536;

pub fn lz78y_with(symbols: &[u8], depth: usize) -> f64 {
    let mut dictionary: HashMap<(usize, u128), Vec<(u8, usize)>> = HashMap::new();
    let mut correct = Vec::with_capacity(symbols.len() - depth - 1);
    for i in depth + 2..=symbols.len() {
        for j in (1..=depth).rev() {
            let key = (j, context_key(&symbols[i - j - 2..i - 1 - 1]));
            let next = symbols[i - 2];
            if !dictionary.contains_key(&key) && dictionary.len() < LZ78Y_DICTIONARY {
                dictionary.insert(key, vec![(next, 0)]);
            }
            if let Some(followers) = dictionary.get_mut(&key) {
                match followers.iter_mut().find(|(symbol, _)| *symbol == next) {
                    Some((_, count)) => *count += 1,
                    None             => followers.push((next, 1)),
                }
            }
        }
        let mut prediction = None;
        let mut best = 0;
        for j in (1..=depth).rev() {
            let key = (j, context_key(&symbols[i - j - 1..i - 1]));
            if let Some(followers) = dictionary.get(&key) {
                let (symbol, count) = followers.iter().max_by_key(|(symbol, count)| (*count, *symbol)).copied().unwrap_or((0, 0));
                if count > best {
                    prediction = Some(symbol);
                    best = count;
                }
            }
        }
        correct.push(prediction == Some(symbols[i - 1]));
    }
    predictor_entropy(&correct, alphabet(symbols))
}

pub fn lz78y(symbols: &[u8]) -> f64 {
    lz78y_with(symbols, LZ78Y_DEPTH)
}
