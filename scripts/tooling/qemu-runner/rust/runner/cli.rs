use crate::vm::machine::Inject;
use std::path::{Path, PathBuf};

pub const USAGE: &str = "qemu_runner --image FILE [--boots 1..10] (--copy | --copy-negative 20..25 | --process | --process-negative 31..37 | --process-disabled | --ordinary | --ordinary-shell | --ordinary-shell-bridge-fault | --ordinary-no-ps2 | --ordinary-soak 1..120 | --ordinary-gui 1..120) [--cpu MODEL] [--smp 1..8]\n\
    \x20   [--root DIR] [--qemu-dir DIR] [--loader FILE] [--out DIR] [--inject drop|stop|quit]\n\
    qemu_runner dump BOOT_DIR\n\
    qemu_runner parity CAMPAIGN_DIR CAMPAIGN_DIR\n\
    qemu_runner desktop [--tcg]";

#[derive(Debug, PartialEq, Eq)]
pub struct RunOptions {
    pub image: PathBuf,
    pub boots: u32,
    pub negative: Option<u32>,
    pub root: Option<PathBuf>,
    pub qemu_dir: PathBuf,
    pub loader: Option<PathBuf>,
    pub out: Option<PathBuf>,
    pub inject: Inject,
    pub process: bool,
    pub ordinary: bool,
    pub cpu: &'static str,
    pub machine: &'static str,
    pub smp: u32,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Mode {
    Desktop { tcg: bool },
    Run(RunOptions),
    Dump(PathBuf),
    Parity(PathBuf, PathBuf),
}

// ====== value_of ======

fn value_of(arguments: &mut impl Iterator<Item = String>, name: &str) -> Result<String, String> {
    arguments.next().ok_or_else(|| format!("{name} requires a value"))
}

// ====== number_in ======

fn number_in(text: &str, name: &str, range: std::ops::RangeInclusive<u32>) -> Result<u32, String> {
    let value = text.parse::<u32>().map_err(|_| format!("{name}: invalid number {text}"))?;
    if range.contains(&value) { Ok(value) } else { Err(format!("{name}: {value} out of range")) }
}

// ====== parse_inject ======

fn parse_inject(text: &str) -> Result<Inject, String> {
    match text {
        "drop" => Ok(Inject::Drop),
        "stop" => Ok(Inject::Stop),
        "quit" => Ok(Inject::Quit),
        _ => Err(format!("--inject: unknown fault {text}")),
    }
}

// ====== parse ======

pub fn parse(arguments: impl IntoIterator<Item = String>) -> Result<Mode, String> {
    let mut arguments = arguments.into_iter().peekable();
    match arguments.peek().map(String::as_str) {
        Some("desktop") => {
            arguments.next();
            let remaining: Vec<String> = arguments.collect();
            return match remaining.as_slice() {
                [] => Ok(Mode::Desktop { tcg: false }),
                [option] if option == "--tcg" => Ok(Mode::Desktop { tcg: true }),
                _ => Err("Usage: qemu_runner desktop [--tcg]".into()),
            };
        }
        Some("dump") => {
            arguments.next();
            return Ok(Mode::Dump(value_of(&mut arguments, "dump")?.into()));
        }
        Some("parity") => {
            arguments.next();
            let first = value_of(&mut arguments, "parity")?;
            return Ok(Mode::Parity(first.into(), value_of(&mut arguments, "parity")?.into()));
        }
        _ => {}
    }
    let mut options = RunOptions {
        image: PathBuf::new(),
        boots: 10,
        negative: None,
        root: None,
        qemu_dir: PathBuf::from("C:/Program Files/qemu"),
        loader: None,
        out: None,
        inject: Inject::None,
        process: false,
        ordinary: false,
        cpu: "max",
        machine: "q35",
        smp: 1,
    };
    let mut selected = false;
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--image" => options.image = value_of(&mut arguments, "--image")?.into(),
            "--boots" => options.boots = number_in(&value_of(&mut arguments, "--boots")?, "--boots", 1..=10)?,
            "--copy" => selected = true,
            "--copy-negative" => {
                let text = value_of(&mut arguments, "--copy-negative")?;
                options.negative = Some(number_in(&text, "--copy-negative", 20..=25)?);
                selected = true;
            }
            "--process" => {
                options.process = true;
                selected = true;
            }
            "--process-negative" => {
                let text = value_of(&mut arguments, "--process-negative")?;
                options.negative = Some(number_in(&text, "--process-negative", 31..=37)?);
                options.process = true;
                selected = true;
            }
            "--process-disabled" => {
                options.negative = Some(0);
                options.process = true;
                options.cpu = "max,-smap";
                selected = true;
            }
            "--ordinary" => {
                options.ordinary = true;
                selected = true;
            }
            "--ordinary-shell" | "--ordinary-shell-bridge-fault" => {
                options.ordinary = true;
                options.negative = Some(if argument == "--ordinary-shell" {
                    crate::scenario::ordinary::SHELL_CASE
                } else {
                    crate::scenario::ordinary::SHELL_BRIDGE_FAULT_CASE
                });
                selected = true;
            }
            "--ordinary-gui" => {
                let minutes = number_in(&value_of(&mut arguments, "--ordinary-gui")?, "--ordinary-gui", 1..=120)?;
                options.ordinary = true;
                options.negative = Some(crate::scenario::ordinary::GUI_CASE_BASE + minutes);
                selected = true;
            }
            "--ordinary-soak" => {
                let minutes = number_in(&value_of(&mut arguments, "--ordinary-soak")?, "--ordinary-soak", 1..=120)?;
                options.ordinary = true;
                options.negative = Some(crate::scenario::ordinary::SOAK_CASE_BASE + minutes);
                selected = true;
            }
            "--ordinary-no-ps2" => {
                options.ordinary = true;
                options.negative = Some(crate::scenario::ordinary::NO_PS2_CASE);
                options.machine = "q35,i8042=off";
                selected = true;
            }
            "--cpu" => {
                options.cpu = match value_of(&mut arguments, "--cpu")?.as_str() {
                    "max" => "max",
                    "max,-smap" => "max,-smap",
                    "max,-smep" => "max,-smep",
                    "max,-smep,-smap" => "max,-smep,-smap",
                    other => return Err(format!("Unknown CPU model: {other}")),
                };
            }
            "--smp" => options.smp = number_in(&value_of(&mut arguments, "--smp")?, "--smp", 1..=8)?,
            "--root" => options.root = Some(value_of(&mut arguments, "--root")?.into()),
            "--qemu-dir" => options.qemu_dir = value_of(&mut arguments, "--qemu-dir")?.into(),
            "--loader" => options.loader = Some(value_of(&mut arguments, "--loader")?.into()),
            "--out" => options.out = Some(value_of(&mut arguments, "--out")?.into()),
            "--inject" => options.inject = parse_inject(&value_of(&mut arguments, "--inject")?)?,
            other => return Err(format!("Unknown argument: {other}")),
        }
    }
    if options.image.as_os_str().is_empty() {
        return Err("--image is required".to_string());
    }
    if !selected {
        return Err("Choose a scenario".to_string());
    }
    Ok(Mode::Run(options))
}

// ====== find_root ======

pub fn find_root(start: &Path) -> Option<PathBuf> {
    start.ancestors().find(|path| path.join("scripts/test/campaigns.json").is_file()).map(Path::to_path_buf)
}
