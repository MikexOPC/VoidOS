use crate::evidence::results::{Value, object, render};
use crate::evidence::sha256::hash_file;
use crate::scenario::copy::run_copy;
use crate::scenario::ordinary::run_ordinary;
use crate::scenario::process::run_process;
use crate::vm::machine::{Limits, Paths, Plan};
use std::io::IsTerminal;
use std::path::Path;

pub const RUNNER: &str = "k5-privilege";

pub struct Campaign<'a> {
    pub out: &'a Path,
    pub image: &'a Path,
    pub paths: &'a Paths,
    pub plan: Plan,
    pub limits: Limits,
    pub negative: Option<u32>,
    pub process: bool,
    pub ordinary: bool,
    pub boots: u32,
}

// ====== failure_text ======

fn failure_text(result: &Value) -> Option<&str> {
    let Value::Object(items) = result else { return None };
    items.iter().find(|(key, _)| key == "Failure").and_then(|(_, value)| match value {
        Value::Text(text) => Some(text.as_str()),
        _ => None,
    })
}

// ====== report_boots ======

fn report_boots(results: &[Value], total: u32) {
    let color = std::io::stdout().is_terminal() && std::env::var_os("NO_COLOR").is_none();
    for (index, result) in results.iter().enumerate() {
        let failure = failure_text(result);
        let label = if failure.is_none() { "[ Pass ]" } else { "[ Fail ]" };
        let code = if failure.is_none() { "\u{1b}[32m" } else { "\u{1b}[31m" };
        println!("\n# ====== Boot {} ====== #", index + 1);
        if color {
            println!("{code}{label}\u{1b}[0m");
        } else {
            println!("{label}");
        }
        if let Some(cause) = failure {
            let cause = cause.split_whitespace().collect::<Vec<_>>().join(" ");
            let cause = if cause.is_empty() { "Errore senza messaggio; consultare le evidenze.".to_string() } else { cause };
            println!("Causa: {cause}");
        }
    }
    if total as usize > results.len() {
        println!("Boot non eseguiti: {}", total as usize - results.len());
    }
}

// ====== run ======

pub fn run(campaign: &Campaign) -> Result<bool, String> {
    let mut results = Vec::new();
    for index in 1..=campaign.boots {
        let directory = campaign.out.join(format!("boot-{index}"));
        let scenario = if campaign.ordinary {
            run_ordinary
        } else if campaign.process {
            run_process
        } else {
            run_copy
        };
        let outcome = scenario(
            &directory,
            campaign.image,
            campaign.paths,
            &campaign.plan,
            campaign.limits,
            campaign.negative,
        );
        match outcome {
            Ok(value) => results.push(value),
            Err(error) => {
                results.push(object(vec![("Failure", Value::Text(error))]));
                break;
            }
        }
    }
    let passed = results.iter().filter(|result| failure_text(result).is_none()).count() as u64;
    let total = u64::from(campaign.boots);
    let image = hash_file(campaign.image).map_err(|error| format!("Image hash: {error}"))?;
    let loader = hash_file(&campaign.paths.loader).map_err(|error| format!("Loader hash: {error}"))?;
    let name = if campaign.ordinary { "k6-ordinary" } else if campaign.process { "k6-process" } else { RUNNER };
    let report = object(vec![
        ("Runner", Value::Text(name.to_string())),
        ("Image", Value::Text(image)),
        ("Loader", Value::Text(loader)),
        ("Passed", Value::Number(passed)),
        ("Total", Value::Number(total)),
        ("Results", Value::List(results.clone())),
    ]);
    std::fs::write(campaign.out.join("results.json"), render(&report)).map_err(|error| format!("Results: {error}"))?;
    report_boots(&results, campaign.boots);
    let success = passed == total;
    println!("RESULT {} {name} {passed}/{total}", if success { "PASS" } else { "FAIL" });
    println!("Evidence: {}", campaign.out.display());
    Ok(success)
}
