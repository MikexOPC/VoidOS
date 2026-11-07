mod binary;
mod counting;
mod predictors;
mod statistics;
mod suffix;
mod track;

use std::process::ExitCode;

// ====== lettura dei campioni ======

pub const RECOMMENDED_SAMPLES: usize = 1_000_000;

fn load(path: &str, bits: u32) -> Result<Vec<u8>, String> {
    let symbols = std::fs::read(path).map_err(|error| format!("{path}: {error}"))?;
    if symbols.len() < 2 {
        return Err("servono almeno due campioni".into());
    }
    if let Some(position) = symbols.iter().position(|symbol| u32::from(*symbol) >> bits != 0) {
        return Err(format!("campione {position} fuori da {bits} bit"));
    }
    Ok(symbols)
}

// ====== report ======

fn print_estimates(title: &str, estimates: &[track::Estimate]) {
    println!("{title}");
    for estimate in estimates {
        match estimate.value {
            Some(value) => println!("  {:<18} {:.6}", estimate.name, value + 0.0),
            None        => println!("  {:<18} non calcolabile", estimate.name),
        }
    }
}

// ====== main ======

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let (Some(path), Some(bits)) = (arguments.first(), arguments.get(1).and_then(|text| text.parse::<u32>().ok())) else {
        eprintln!("uso: entropy <file di campioni, un byte per campione> <bit per campione 1..8>");
        return ExitCode::from(2);
    };
    if !(1..=8).contains(&bits) {
        eprintln!("bit per campione fuori da 1..8");
        return ExitCode::from(2);
    }
    let symbols = match load(path, bits) {
        Ok(symbols) => symbols,
        Err(error) => {
            eprintln!("[ FAIL ] {error}");
            return ExitCode::FAILURE;
        }
    };
    if symbols.len() < RECOMMENDED_SAMPLES {
        println!("[ WARN ] {} campioni: SP 800-90B 3.1.1 ne chiede almeno {RECOMMENDED_SAMPLES}", symbols.len());
    }
    let assessment = track::assess(&symbols, bits);
    print_estimates("H_original", &assessment.original);
    if !assessment.bits.is_empty() {
        print_estimates("H_bitstring", &assessment.bits);
    }
    println!("min-entropia per campione: {:.6} bit su {bits}", assessment.per_sample + 0.0);
    ExitCode::SUCCESS
}
