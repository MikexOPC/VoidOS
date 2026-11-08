use crate::{depfile, image, plan, sha256};
use std::fs;
use std::io::Write;
use std::path::Path;
use std::process::Command;

const NASM_SHA256: &str = "5f59c792cc3b92316a4e6d61cae192588a0d8f8fa30b33988a0f2230e5a9c22c";
const AP_TEMPLATE: &str = "ap-template.bin";
const AP_TEMPLATE_SIZE: u64 = 4096;
const SOURCE_SUFFIXES: [&str; 3] = [".c", ".asm", ".rs"];

// ====== write_audit ======

fn write_audit(output: &str, line: &str) -> Result<(), String> {
    fs::write(format!("{output}{}", plan::AUDIT_SUFFIX), format!("{line}\n"))
        .map_err(|error| format!("{output}: {error}"))
}

// ====== checked ======

fn checked(command: &mut Command, name: &str) -> Result<std::process::Output, String> {
    let result = command.output().map_err(|error| format!("{name}: {error}"))?;
    if !result.status.success() {
        std::io::stdout().write_all(&result.stdout).ok();
        std::io::stderr().write_all(&result.stderr).ok();
        return Err(format!("Compilazione fallita: {name}"));
    }
    Ok(result)
}

// ====== sources_in ======

fn sources_in(arguments: &[String]) -> Vec<String> {
    arguments
        .iter()
        .filter(|argument| SOURCE_SUFFIXES.iter().any(|suffix| argument.ends_with(suffix)))
        .cloned()
        .collect()
}

// ====== run_cl ======

fn run_cl(program: &str, arguments: &[String], dep: &str) -> Result<Vec<String>, String> {
    let raw = format!("{dep}.raw");
    checked(Command::new(program).args(arguments).args(["/sourceDependencies", &raw]), program)?;
    let text = fs::read_to_string(&raw).map_err(|error| format!("{raw}: {error}"))?;
    fs::remove_file(&raw).ok();
    depfile::parse_source_dependencies(&text)
}

// ====== run_ml64 ======

fn run_ml64(program: &str, arguments: &[String]) -> Result<Vec<String>, String> {
    for source in sources_in(arguments) {
        let text = fs::read_to_string(&source).map_err(|error| format!("{source}: {error}"))?;
        if depfile::declares_include(&text) {
            return Err(format!("{source}: ml64 records no dependencies, include is not tracked."));
        }
    }
    checked(Command::new(program).args(arguments), program)?;
    Ok(Vec::new())
}

// ====== run_nasm ======

fn run_nasm(program: &str, arguments: &[String], dep: &str) -> Result<Vec<String>, String> {
    let hash = sha256::hash_file(Path::new(program)).map_err(|error| format!("{program}: {error}"))?;
    if !hash.eq_ignore_ascii_case(NASM_SHA256) {
        return Err("Pinned NASM tools absent or changed. Run assembler.ps1 -Download explicitly.".into());
    }
    let raw = format!("{dep}.raw");
    checked(Command::new(program).args(arguments).args(["-MD", &raw]), program)?;
    let text = fs::read_to_string(&raw).map_err(|error| format!("{raw}: {error}"))?;
    fs::remove_file(&raw).ok();
    Ok(depfile::parse_prerequisites(&text))
}

// ====== run_rustc ======

fn run_rustc(program: &str, arguments: &[String], dep: &str) -> Result<Vec<String>, String> {
    checked(Command::new(program).args(arguments), program)?;
    let raw = format!("{dep}.raw");
    let mut filtered = Vec::new();
    let mut skip = false;
    for argument in arguments {
        if skip {
            skip = false;
        } else if argument == "-o" {
            skip = true;
        } else if !argument.starts_with("--emit") {
            filtered.push(argument.clone());
        }
    }
    filtered.push(format!("--emit=dep-info={raw}"));
    checked(Command::new(program).args(&filtered), program)?;
    let text = fs::read_to_string(&raw).map_err(|error| format!("{raw}: {error}"))?;
    fs::remove_file(&raw).ok();
    Ok(depfile::parse_prerequisites(&text))
}

// ====== run ======

pub fn run(fields: &[String]) -> Result<(), String> {
    let [_, output, dep, program, arguments @ ..] = fields else {
        return Err("Invalid compile step.".into());
    };
    let mut audit = vec![program.clone()];
    audit.extend(arguments.iter().cloned());
    write_audit(output, &plan::audit_line(&audit))?;

    let name = program.rsplit('/').next().unwrap_or(program).to_ascii_lowercase();
    let mut prerequisites = match name.as_str() {
        "cl.exe" => run_cl(program, arguments, dep)?,
        "ml64.exe" => run_ml64(program, arguments)?,
        "nasm.exe" => run_nasm(program, arguments, dep)?,
        "rustc.exe" | "rustc" => run_rustc(program, arguments, dep)?,
        _ => return Err(format!("Unexpected compiler: {program}")),
    };
    if output == AP_TEMPLATE {
        let size = fs::metadata(output).map_err(|error| error.to_string())?.len();
        if size != AP_TEMPLATE_SIZE {
            return Err("Invalid AP template size".into());
        }
    }
    prerequisites.extend(sources_in(arguments));
    fs::write(dep, depfile::render(output, &prerequisites)).map_err(|error| format!("{dep}: {error}"))
}

// ====== link ======

pub fn link(plan_file: &str) -> Result<(), String> {
    let text = fs::read_to_string(plan_file).map_err(|error| format!("{plan_file}: {error}"))?;
    let row: Vec<String> = text
        .lines()
        .find(|line| line.split('\t').next().is_some_and(|tool| tool.ends_with("/link.exe")))
        .ok_or("Plan has no link command.")?
        .split('\t')
        .map(String::from)
        .collect();

    let mut response = Vec::from([0xEF, 0xBB, 0xBF]);
    for argument in &row[1..] {
        response.extend_from_slice(format!("\"{argument}\"\r\n").as_bytes());
    }
    let path = "kernel.link.rsp";
    fs::write(path, response).map_err(|error| format!("{path}: {error}"))?;
    write_audit("kernel.exe", &plan::audit_line(&row))?;
    let result = checked(Command::new(&row[0]).arg(format!("@{path}")), &row[0])?;
    std::io::stdout().write_all(&result.stdout).ok();
    Ok(())
}

// ====== convert_image ======

pub fn convert_image(input: &str, output: &str) -> Result<(), String> {
    let input = plan::normal(input);
    let output = plan::normal(output);
    let data = fs::read(&input).map_err(|error| format!("{input}: {error}"))?;
    let elf = image::convert(&data, image::image_base()?)?;
    let pending = format!("{output}.pending");
    fs::write(&pending, elf).map_err(|error| format!("{pending}: {error}"))?;
    fs::rename(&pending, &output).map_err(|error| format!("{output}: {error}"))?;
    write_audit(
        output.rsplit('/').next().unwrap_or(&output),
        &format!("convert-image.ps1\t-Image\t{input}\t-Output\t{output}"),
    )
}
