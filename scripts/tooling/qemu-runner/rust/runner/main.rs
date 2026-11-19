use qemu_runner_core::campaign::{Campaign, run};
use qemu_runner_core::cli::{self, Mode, RunOptions, USAGE};
use qemu_runner_core::parity;
use qemu_runner_core::vm::job::{SystemAwake, bind_children_to_runner};
use qemu_runner_core::vm::machine::{Limits, Paths, Plan};
use std::path::{PathBuf, absolute};
use std::process::ExitCode;
use std::time::{SystemTime, UNIX_EPOCH};

const LOADER: &str = "arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI";

// ====== unique_out ======

fn unique_out(root: &std::path::Path) -> PathBuf {
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |time| time.as_nanos());
    root.join("kernel/build").join(format!("qemu-runner-{stamp:x}-{:x}", std::process::id()))
}

// ====== run_campaign ======

fn run_campaign(options: RunOptions) -> Result<bool, String> {
    bind_children_to_runner().map_err(|error| format!("Job object: {error}"))?;
    let _awake = SystemAwake::new().map_err(|error| format!("Host execution state: {error}"))?;
    let current = std::env::current_exe().map_err(|error| error.to_string())?;
    let root = match options.root {
        Some(root) => root,
        None => cli::find_root(&current).ok_or("Repository root not found; use --root")?,
    };
    let root = absolute(root).map_err(|error| error.to_string())?;
    let out = absolute(options.out.unwrap_or_else(|| unique_out(&root))).map_err(|error| error.to_string())?;
    std::fs::create_dir(&out).map_err(|error| format!("{}: {error}", out.display()))?;
    let paths = Paths {
        qemu_dir: options.qemu_dir,
        loader: absolute(options.loader.unwrap_or_else(|| root.join(LOADER))).map_err(|error| error.to_string())?,
    };
    let image = absolute(&options.image).map_err(|error| error.to_string())?;
    run(&Campaign {
        out: &out,
        image: &image,
        paths: &paths,
        plan: Plan { memory: 128, smp: options.smp, inject: options.inject, cpu: options.cpu, machine: options.machine },
        limits: Limits::standard(),
        negative: options.negative,
        process: options.process,
        ordinary: options.ordinary,
        boots: options.boots,
    })
}

// ====== run_parity ======

fn run_parity(first: PathBuf, second: PathBuf) -> Result<bool, String> {
    let differences = parity::compare(&first, &second)?;
    for difference in &differences {
        println!("{difference}");
    }
    println!("PARITY {} {} {}", if differences.is_empty() { "SAME" } else { "DIFFERENT" }, first.display(), second.display());
    Ok(differences.is_empty())
}

// ====== main ======

fn main() -> ExitCode {
    let mode = match cli::parse(std::env::args().skip(1)) {
        Ok(mode) => mode,
        Err(error) => {
            eprintln!("{error}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    let outcome = match mode {
        Mode::Run(options) => run_campaign(options),
        Mode::Dump(directory) => {
            print!("{}", parity::dump_boot(&directory));
            Ok(true)
        }
        Mode::Parity(first, second) => run_parity(first, second),
    };
    match outcome {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::from(1),
        Err(error) => {
            eprintln!("{error}");
            ExitCode::from(1)
        }
    }
}
