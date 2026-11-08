mod depfile;
mod image;
mod layout;
mod parity;
mod plan;
#[path = "../../qemu-runner/rust/evidence/sha256.rs"]
mod sha256;
mod step;

use std::fs;
use std::process::ExitCode;

// ====== context ======

fn context(arguments: &[String]) -> Result<plan::Context, String> {
    let mut found = plan::Context {
        planner: String::new(),
        repo: String::new(),
        out: String::new(),
        toolchain: None,
        rustc: String::new(),
        nasm: String::new(),
        sdk: None,
        profile: String::new(),
    };
    for pair in arguments.chunks(2) {
        let [key, value] = pair else {
            return Err("Options need a value.".into());
        };
        match key.as_str() {
            "--planner" => found.planner = value.clone(),
            "--repo" => found.repo = value.clone(),
            "--out" => found.out = value.clone(),
            "--toolchain" => found.toolchain = Some(value.clone()),
            "--rustc" => found.rustc = value.clone(),
            "--nasm" => found.nasm = value.clone(),
            "--sdk" => found.sdk = Some(value.clone()),
            "--profile" => found.profile = value.clone(),
            _ => return Err(format!("Unknown option: {key}")),
        }
    }
    let missing = [&found.planner, &found.repo, &found.out, &found.rustc, &found.nasm];
    if missing.iter().any(|value| value.is_empty()) {
        return Err("Missing required option.".into());
    }
    Ok(found)
}

// ====== emit_plan ======

fn emit_plan(arguments: &[String]) -> Result<(), String> {
    let context = context(arguments)?;
    layout::check_copies(&context.repo)?;
    let rows = plan::resolved_plan(&context)?;
    let text: String = rows.iter().map(|row| row.join("\t") + "\n").collect();
    let out = plan::normal(&context.out);
    fs::write(format!("{out}/{}", plan::PLAN_FILE), &text).map_err(|error| error.to_string())?;
    plan::remove_stale_audits(&out, &rows)?;

    let link_row = rows
        .iter()
        .find(|row| row[0].ends_with("/link.exe"))
        .ok_or("Plan has no link command.")?;
    let hash = sha256::hash_bytes(link_row.join("\t").as_bytes());
    for line in plan::meson_rows(&rows, &hash, &out)? {
        println!("{line}");
    }
    Ok(())
}

// ====== dispatch ======

fn dispatch(arguments: &[String]) -> Result<(), String> {
    match arguments {
        [mode, rest @ ..] if mode == "plan" => emit_plan(rest),
        [mode, rest @ ..] if mode == "run" => step::run(rest),
        [mode, plan_file, _hash] if mode == "link" => step::link(plan_file),
        [mode, input, output] if mode == "image" => step::convert_image(input, output),
        [mode, rest @ ..] if mode == "parity" => {
            let context = context(rest)?;
            let count = parity::check(&context)?;
            println!("[ Pass ] {} {count} commands equal to the plan", context.profile);
            Ok(())
        }
        _ => Err("Usage: kernel_build plan|run|link|image|parity ...".into()),
    }
}

// ====== main ======

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    match dispatch(&arguments) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
