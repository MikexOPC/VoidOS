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

const MODULES: [Module; 2] = [
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
