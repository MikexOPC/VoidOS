use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

// ====== moduli ======

struct Module {
    name   : &'static str,
    source : &'static str,
    test   : &'static str,
}

const MODULES: [Module; 4] = [
    Module {
        name   : "bounds",
        source : "lib/security/bounds/rust/bounds.rs",
        test   : "test/lib/security/bounds/rust/bounds_test.rs",
    },
    Module {
        name   : "entropy",
        source : "lib/security/entropy/rust/health.rs",
        test   : "test/lib/security/entropy/rust/health_test.rs",
    },
    Module {
        name   : "memory",
        source : "lib/security/memory/rust/memory.rs",
        test   : "test/lib/security/memory/rust/memory_test.rs",
    },
    Module {
        name   : "sha256",
        source : "lib/security/hash/sha256/rust/sha256.rs",
        test   : "test/lib/security/hash/sha256/rust/sha256_test.rs",
    },
];

const RUNNER: &str = "test/lib/security/runner.rs";

// ====== esecuzione controllata ======

fn run(label: &str, program: &Path, arguments: &[&str]) -> Result<(), String> {
    println!("[ INFO ] {label}");
    let status = Command::new(program)
        .args(arguments)
        .status()
        .map_err(|error| format!("{label}: {} non avviabile: {error}", program.display()))?;
    if status.success() {
        println!("[ OK ] {label}");
        Ok(())
    } else {
        Err(format!("{label}: {status}"))
    }
}

fn text(path: &Path) -> &str {
    path.to_str().expect("percorso UTF-8")
}

// ====== test host debug e release ======

fn host_tests(module: &Module, project: &Path, out: &Path) -> Result<(), String> {
    let test = project.join(module.test);
    for (mode, extra) in [("debug", None), ("release", Some("opt-level=2"))] {
        let exe = out.join(format!("{}-{mode}.exe", module.name));
        let mut arguments = vec!["--edition=2024", "--test", "-D", "warnings", "-C", "overflow-checks=yes", text(&test), "-o", text(&exe)];
        if let Some(level) = extra {
            arguments.extend(["-C", level]);
        }
        run(&format!("{} build {mode}", module.name), Path::new("rustc"), &arguments)?;
        run(&format!("{} test {mode}", module.name), &exe, &[])?;
    }
    Ok(())
}

// ====== clippy e oggetto freestanding ======

fn clippy(label: &str, source: &Path, crate_type: &str, metadata: &Path) -> Result<(), String> {
    run(label, Path::new("clippy-driver"), &[
        "--edition=2024", crate_type, "-D", "warnings", "-D", "clippy::all",
        "--emit=metadata", text(source), "-o", text(metadata),
    ])
}

fn static_checks(module: &Module, project: &Path, out: &Path) -> Result<(), String> {
    let source = project.join(module.source);
    let test = project.join(module.test);
    clippy(&format!("{} clippy test", module.name), &test, "--test", &out.join(format!("{}-test.rmeta", module.name)))?;
    clippy(&format!("{} clippy lib", module.name), &source, "--crate-type=lib", &out.join(format!("{}.rmeta", module.name)))?;
    let object = out.join(format!("{}.obj", module.name));
    run(&format!("{} no_std object", module.name), Path::new("rustc"), &[
        "--edition=2024", "--crate-type=lib", "--emit=obj", "--target", "x86_64-pc-windows-msvc",
        "-C", "panic=abort", "-C", "opt-level=2", "-D", "warnings", text(&source), "-o", text(&object),
    ])
}

// ====== azzeramento non eliminabile ======

const PROBE: &str = "test/lib/security/memory/rust/probe.rs";

fn zero_stores(assembly: &str, function: &str) -> Option<usize> {
    let start = assembly.lines().position(|line| line == format!("{function}:"))?;
    let body = assembly.lines().skip(start + 1).take_while(|line| !line.trim_start().starts_with("ret"));
    Some(body.filter(|line| line.contains("$0,")).count())
}

fn wipe_probe(project: &Path, out: &Path) -> Result<(), String> {
    let assembly = out.join("probe.s");
    run("memory volatile probe", Path::new("rustc"), &[
        "--edition=2024", "--crate-type=lib", "--emit=asm", "--target", "x86_64-pc-windows-msvc",
        "-C", "panic=abort", "-C", "opt-level=2", "-D", "warnings", text(&project.join(PROBE)), "-o", text(&assembly),
    ])?;
    let listing = fs::read_to_string(&assembly).map_err(|error| format!("probe.s: {error}"))?;
    let kept = zero_stores(&listing, "probe_volatile").ok_or("probe_volatile assente")?;
    let removed = zero_stores(&listing, "probe_plain").ok_or("probe_plain assente")?;
    println!("[ INFO ] store di zero: volatile {kept}, normale {removed}");
    if kept >= 8 && removed == 0 {
        Ok(())
    } else {
        Err(format!("azzeramento non dimostrato: volatile {kept}, normale {removed}"))
    }
}

// ====== main ======

fn suite(project: &Path, out: &Path, only: Option<&str>) -> Result<usize, String> {
    clippy("runner clippy", &project.join(RUNNER), "--crate-type=bin", &out.join("runner.rmeta"))?;
    let selected: Vec<&Module> = MODULES.iter().filter(|module| only.is_none_or(|name| name == module.name)).collect();
    if selected.is_empty() {
        return Err(format!("modulo sconosciuto: {}", only.unwrap_or_default()));
    }
    for module in &selected {
        host_tests(module, project, out)?;
        static_checks(module, project, out)?;
        if module.name == "memory" {
            wipe_probe(project, out)?;
        }
    }
    Ok(selected.len())
}

fn main() -> ExitCode {
    let arguments: Vec<String> = env::args().skip(1).collect();
    let (Some(project), Some(out)) = (arguments.first(), arguments.get(1)) else {
        eprintln!("uso: runner <progetto> <output> [modulo]");
        return ExitCode::from(2);
    };
    let (project, out) = (PathBuf::from(project), PathBuf::from(out));
    if let Err(error) = fs::create_dir_all(&out) {
        eprintln!("[ FAIL ] output {}: {error}", out.display());
        return ExitCode::FAILURE;
    }
    match suite(&project, &out, arguments.get(2).map(String::as_str)) {
        Ok(count) => {
            println!("PASS: security host suite, {count} moduli; evidence {}", out.display());
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("[ FAIL ] {error}");
            ExitCode::FAILURE
        }
    }
}
