use crate::vm::machine::Inject;
use std::path::{Path, PathBuf};

pub const USAGE: &str = "qemu_runner --image FILE [--boots 1..10] (--copy | --copy-negative 20..25 | --process | --process-negative 31..35 | --process-disabled)\n\
    \x20   [--root DIR] [--qemu-dir DIR] [--loader FILE] [--out DIR] [--inject drop|stop|quit]\n\
    qemu_runner dump BOOT_DIR\n\
    qemu_runner parity CAMPAIGN_DIR CAMPAIGN_DIR";

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
    pub cpu: &'static str,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Mode {
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
        cpu: "max",
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
                options.negative = Some(number_in(&text, "--process-negative", 31..=35)?);
                options.process = true;
                selected = true;
            }
            "--process-disabled" => {
                options.negative = Some(0);
                options.process = true;
                options.cpu = "max,-smap";
                selected = true;
            }
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
