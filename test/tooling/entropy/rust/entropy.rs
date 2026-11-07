#[path = "estimators/binary.rs"]
mod binary;
#[path = "estimators/counting.rs"]
mod counting;
#[path = "estimators/predictors.rs"]
mod predictors;
mod restart;
#[path = "estimators/statistics.rs"]
mod statistics;
#[path = "estimators/suffix.rs"]
mod suffix;
mod track;

use std::process::ExitCode;

// ====== lettura dei campioni ======

pub const RECOMMENDED_SAMPLES: usize = 1_000_000;

const USAGE: &str = "uso: entropy <file> <bit 1..8>\n     entropy restart <file 1000x1000> <bit 1..8> <H_I>";

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

fn parse_bits(text: Option<&String>) -> Result<u32, String> {
    match text.and_then(|value| value.parse::<u32>().ok()) {
        Some(bits) if (1..=8).contains(&bits) => Ok(bits),
        _ => Err("bit per campione fuori da 1..8".into()),
    }
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

// ====== valutazione iniziale (3.1.3) ======

fn initial(arguments: &[String]) -> Result<(), String> {
    let path = arguments.first().ok_or(USAGE)?;
    let bits = parse_bits(arguments.get(1))?;
    let symbols = load(path, bits)?;
    if symbols.len() < RECOMMENDED_SAMPLES {
        println!("[ WARN ] {} campioni: SP 800-90B 3.1.1 ne chiede almeno {RECOMMENDED_SAMPLES}", symbols.len());
    }
    let assessment = track::assess(&symbols, bits);
    print_estimates("H_original", &assessment.original);
    if !assessment.bits.is_empty() {
        print_estimates("H_bitstring", &assessment.bits);
    }
    println!("min-entropia per campione: {:.6} bit su {bits}", assessment.per_sample + 0.0);
    Ok(())
}

// ====== test di riavvio (3.1.4) ======

fn restarts(arguments: &[String]) -> Result<bool, String> {
    let path = arguments.first().ok_or(USAGE)?;
    let bits = parse_bits(arguments.get(1))?;
    let initial = arguments.get(2).and_then(|text| text.parse::<f64>().ok()).ok_or(USAGE)?;
    if !(0.0..=f64::from(bits)).contains(&initial) {
        return Err(format!("H_I deve stare fra 0 e {bits}"));
    }
    let data = load(path, bits)?;
    if data.len() != restart::RESTARTS * restart::RESTART_SAMPLES {
        return Err(format!("servono {} riavvii x {} campioni", restart::RESTARTS, restart::RESTART_SAMPLES));
    }
    let matrix = restart::Matrix { data: &data, restarts: restart::RESTARTS, samples: restart::RESTART_SAMPLES };
    let report = restart::restart_test(&matrix, bits, initial);
    println!("X_R {}, X_C {}, P(X >= X_max) {:.3e}", report.most_in_rows, report.most_in_columns, report.probability);
    if !report.sanity {
        println!("[ FAIL ] controllo di sanita: nessuna stima");
        return Ok(false);
    }
    print_estimates("H_r", &report.rows);
    print_estimates("H_c", &report.columns);
    match report.assessment {
        Some(value) => println!("min(H_r, H_c, H_I): {:.6} bit su {bits}", value + 0.0),
        None        => println!("[ FAIL ] min(H_r, H_c) < H_I/2: nessuna stima"),
    }
    Ok(report.assessment.is_some())
}

// ====== main ======

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let outcome = if arguments.first().is_some_and(|command| command == "restart") {
        restarts(&arguments[1..])
    } else {
        initial(&arguments).map(|()| true)
    };
    match outcome {
        Ok(true)  => ExitCode::SUCCESS,
        Ok(false) => ExitCode::FAILURE,
        Err(error) => {
            eprintln!("[ FAIL ] {error}");
            ExitCode::from(2)
        }
    }
}
