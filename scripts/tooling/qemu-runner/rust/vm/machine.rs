use crate::evidence::results::{Value, object, render};
use crate::evidence::serial::SerialLog;
use crate::evidence::sha256::hash_file;
use std::fs::{self, File};
use std::io::Write;
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::mpsc::{Sender, channel};
use std::time::{Duration, Instant};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;
const WAIT_POLL: Duration = Duration::from_millis(1);

#[derive(Clone, Debug)]
pub struct Paths {
    pub qemu_dir: PathBuf,
    pub loader: PathBuf,
}

#[derive(Clone, Copy, Debug)]
pub struct Limits {
    pub wait: Duration,
    pub window: Duration,
    pub window_poll: Duration,
    pub negative: Duration,
    pub close: Duration,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Inject {
    None,
    Drop,
    Stop,
    Quit,
}

#[derive(Clone, Copy, Debug)]
pub struct Plan {
    pub memory: u32,
    pub smp: u32,
    pub inject: Inject,
}

pub struct Vm {
    pub directory: PathBuf,
    pub serial: PathBuf,
    pub monitor: PathBuf,
    pub trace: PathBuf,
    pub limits: Limits,
    program: PathBuf,
    arguments: Vec<String>,
    inject: Inject,
    inject_on: Option<String>,
    child: Option<Child>,
    writer: Option<Sender<String>>,
    pid: u32,
}

// ====== Limits ======

impl Limits {
    pub fn standard() -> Limits {
        Limits {
            wait: Duration::from_secs(40),
            window: Duration::from_secs(8),
            window_poll: Duration::from_millis(300),
            negative: Duration::from_secs(60),
            close: Duration::from_secs(5),
        }
    }
}

// ====== posix ======

fn posix(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}

// ====== prepare_esp ======

fn prepare_esp(directory: &Path, image: &Path, paths: &Paths) -> Result<PathBuf, String> {
    fs::create_dir(directory).map_err(|error| format!("Boot directory: {error}"))?;
    let esp = directory.join("esp");
    let boot = esp.join("EFI/BOOT");
    fs::create_dir_all(&boot).map_err(|error| format!("ESP: {error}"))?;
    let kernel = boot.join("kernel.elf");
    fs::copy(image, &kernel).map_err(|error| format!("ESP image: {error}"))?;
    fs::copy(&paths.loader, boot.join("BOOTX64.EFI")).map_err(|error| format!("ESP loader: {error}"))?;
    let source = hash_file(image).map_err(|error| format!("Image hash: {error}"))?;
    let copied = hash_file(&kernel).map_err(|error| format!("ESP hash: {error}"))?;
    if source != copied {
        return Err("ESP image mismatch".to_string());
    }
    fs::write(boot.join("boot-check.txt"), b"OS boot check\r\n").map_err(|error| format!("ESP file: {error}"))?;
    Ok(esp)
}

// ====== arguments ======

pub fn arguments(directory: &Path, paths: &Paths, plan: &Plan) -> Vec<String> {
    let share = paths.qemu_dir.join("share");
    let mut list: Vec<String> = [
        "-machine", "q35", "-cpu", "max", "-accel", "tcg", "-m",
    ].map(String::from).to_vec();
    list.push(plan.memory.to_string());
    list.push("-smp".to_string());
    list.push(plan.smp.to_string());
    list.extend(["-net", "none", "-vga", "std", "-display", "none", "-monitor", "stdio", "-no-reboot"].map(String::from));
    list.push("-serial".to_string());
    list.push(format!("file:{}", posix(&directory.join("serial.log"))));
    list.extend(["-d", "int,cpu_reset", "-D"].map(String::from));
    list.push(posix(&directory.join("qemu.log")));
    list.push("-drive".to_string());
    list.push(format!("if=pflash,format=raw,readonly=on,file={}", posix(&share.join("edk2-x86_64-code.fd"))));
    list.push("-drive".to_string());
    list.push(format!("if=pflash,format=raw,file={}", posix(&directory.join("vars.fd"))));
    list.push("-drive".to_string());
    list.push(format!("format=raw,file=fat:rw:{}", posix(&directory.join("esp"))));
    list
}

// ====== Vm ======

impl Vm {
    pub fn start(directory: &Path, image: &Path, paths: &Paths, plan: &Plan, limits: Limits) -> Result<Vm, String> {
        prepare_esp(directory, image, paths)?;
        fs::copy(paths.qemu_dir.join("share/edk2-i386-vars.fd"), directory.join("vars.fd"))
            .map_err(|error| format!("OVMF variables: {error}"))?;
        let list = arguments(directory, paths, plan);
        let monitor = directory.join("monitor.log");
        let monitor_file = File::create(&monitor).map_err(|error| format!("Monitor log: {error}"))?;
        let error_file = File::create(directory.join("stderr.log")).map_err(|error| format!("Stderr log: {error}"))?;
        let program = paths.qemu_dir.join("qemu-system-x86_64.exe");
        let mut child = Command::new(&program)
            .args(&list)
            .stdin(Stdio::piped())
            .stdout(monitor_file)
            .stderr(error_file)
            .creation_flags(CREATE_NO_WINDOW)
            .spawn()
            .map_err(|error| format!("QEMU start: {error}"))?;
        let pid = child.id();
        let (sender, receiver) = channel::<String>();
        let mut stdin = child.stdin.take().ok_or("QEMU stdin unavailable")?;
        std::thread::spawn(move || {
            for text in receiver {
                let written = stdin.write_all(text.as_bytes()).and_then(|()| stdin.flush());
                if written.is_err() {
                    break;
                }
            }
        });
        let vm = Vm {
            directory: directory.to_path_buf(),
            serial: directory.join("serial.log"),
            monitor,
            trace: directory.join("qemu.log"),
            limits,
            program,
            arguments: list,
            inject: plan.inject,
            inject_on: None,
            child: Some(child),
            writer: Some(sender),
            pid,
        };
        vm.record(None)?;
        Ok(vm)
    }

    pub fn pid(&self) -> u32 {
        self.pid
    }

    pub fn inject_on(&mut self, command: &str) {
        self.inject_on = Some(command.to_string());
    }

    pub fn alive(&mut self) -> bool {
        self.child.as_mut().is_some_and(|child| matches!(child.try_wait(), Ok(None)))
    }

    pub fn log(&self) -> SerialLog {
        SerialLog::from_file(&self.serial)
    }

    pub fn monitor_size(&self) -> Result<u64, String> {
        fs::metadata(&self.monitor).map(|meta| meta.len()).map_err(|error| format!("Monitor log: {error}"))
    }

    pub fn monitor_bytes(&self) -> Result<Vec<u8>, String> {
        fs::read(&self.monitor).map_err(|error| format!("Monitor log: {error}"))
    }

    pub fn trace_bytes(&self) -> Result<Vec<u8>, String> {
        fs::read(&self.trace).map_err(|error| format!("QEMU trace: {error}"))
    }

    pub fn command(&mut self, text: &str) {
        let replaced = if self.inject_on.as_deref() == Some(text) {
            self.inject_on = None;
            match self.inject {
                Inject::None => Some(text),
                Inject::Drop => None,
                Inject::Stop => Some("stop"),
                Inject::Quit => Some("quit"),
            }
        } else {
            Some(text)
        };
        if let (Some(line), Some(writer)) = (replaced, self.writer.as_ref()) {
            let _ = writer.send(format!("{line}\n"));
        }
    }

    pub fn wait(&mut self, marker: &str, occurrences: usize, level: Option<&str>) -> Result<SerialLog, String> {
        let deadline = Instant::now() + self.limits.wait;
        while Instant::now() < deadline {
            let log = self.log();
            if log.terminal() {
                return Err(format!("Kernel failure: {}", self.serial.display()));
            }
            if log.count(marker, level) >= occurrences {
                return Ok(log);
            }
            if !self.alive() {
                return Err(format!("Owned VM exited before {marker}"));
            }
            std::thread::sleep(WAIT_POLL);
        }
        Err(format!("Missing {marker}: {}", self.serial.display()))
    }

    pub fn close(&mut self) -> Result<(), String> {
        let Some(mut child) = self.child.take() else { return Ok(()) };
        let started = Instant::now();
        let exited_alone = matches!(child.try_wait(), Ok(Some(_)));
        let killed = !exited_alone && child.kill().is_ok();
        self.writer = None;
        let deadline = started + self.limits.close;
        let status = loop {
            match child.try_wait() {
                Ok(Some(status)) => break Some(status),
                Ok(None) if Instant::now() < deadline => std::thread::sleep(WAIT_POLL),
                _ => break None,
            }
        };
        let closure = object(vec![
            ("ExitedAlone", Value::Bool(exited_alone)),
            ("KilledByRunner", Value::Bool(killed)),
            ("ExitCode", status.and_then(|item| item.code()).map_or(Value::Text("unknown".to_string()), |code| {
                Value::Number(u64::from(code as u32))
            })),
            ("CloseMilliseconds", Value::Number(started.elapsed().as_millis() as u64)),
            ("Terminated", Value::Bool(status.is_some())),
        ]);
        self.record(Some(closure))?;
        if status.is_none() {
            return Err(format!("Owned VM {} did not terminate", self.pid));
        }
        Ok(())
    }

    fn record(&self, closure: Option<Value>) -> Result<(), String> {
        let inject = format!("{:?}", self.inject);
        let mut items = vec![
            ("Pid", Value::Number(u64::from(self.pid))),
            ("Program", Value::Text(posix(&self.program))),
            ("Inject", Value::Text(inject)),
            ("Arguments", Value::List(self.arguments.iter().cloned().map(Value::Text).collect())),
        ];
        if let Some(closure) = closure {
            items.push(("Closure", closure));
        }
        fs::write(self.directory.join("vm.json"), render(&object(items))).map_err(|error| format!("VM record: {error}"))
    }
}

// ====== Drop ======

impl Drop for Vm {
    fn drop(&mut self) {
        let _ = self.close();
    }
}
