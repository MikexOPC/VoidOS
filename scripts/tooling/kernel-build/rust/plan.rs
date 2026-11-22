use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

pub const PLAN_FILE: &str = "plan.tsv";
pub const AUDIT_SUFFIX: &str = ".argv";
const MSVC_ROOT: &str = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC";
const SDK_ROOT: &str = "C:/Program Files (x86)/Windows Kits/10/Include";

pub struct Context {
    pub planner: String,
    pub repo: String,
    pub out: String,
    pub toolchain: Option<String>,
    pub rustc: String,
    pub nasm: String,
    pub sdk: Option<String>,
    pub profile: String,
}

// ====== normal ======

pub fn normal(path: &str) -> String {
    path.replace('\\', "/")
}

// ====== profile_arguments ======

pub fn profile_arguments(profile: &str) -> Result<Vec<String>, String> {
    let fixed: &[&str] = match profile {
        "ordinary" => &[],
        "periodic" => &["-TestSchedulerPeriodic"],
        "ap-timer" => &["-TestApService", "ap-timer"],
        "k6-process" => &["-TestK6Process"],
        _ => {
            let case = profile
                .strip_prefix("k6-process-")
                .and_then(|case| case.parse::<u8>().ok())
                .filter(|case| (31..=37).contains(case))
                .ok_or_else(|| format!("Unsupported Meson kernel profile: {profile}"))?;
            return Ok(vec!["-TestK6Process".into(), "-TestK6Negative".into(), case.to_string()]);
        }
    };
    Ok(fixed.iter().map(|argument| argument.to_string()).collect())
}

// ====== newest_toolchain ======

pub fn newest_toolchain(root: &Path) -> Result<String, String> {
    let mut versions: Vec<PathBuf> = fs::read_dir(root)
        .map_err(|error| format!("{}: {error}", root.display()))?
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.is_dir())
        .collect();
    versions.sort();
    let newest = versions.pop().ok_or("MSVC toolchain absent.")?;
    Ok(normal(&newest.join("bin/Hostx64/x64").to_string_lossy()))
}

// ====== newest_sdk ======

pub fn newest_sdk(root: &Path) -> Result<String, String> {
    let mut versions: Vec<PathBuf> = fs::read_dir(root)
        .map_err(|error| format!("{}: {error}", root.display()))?
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.join("ucrt/stddef.h").is_file())
        .collect();
    versions.sort();
    let newest = versions.pop().ok_or("Header UCRT del Windows SDK assenti.")?;
    Ok(normal(&newest.to_string_lossy()))
}

// ====== resolve ======

pub fn resolve(field: &str, bin: &str, sdk: &str, repo: &str, out: &str) -> String {
    field
        .replace("{repo}/plan-msvc/bin/Hostx64/x64", bin)
        .replace("{repo}/plan-sdk", sdk)
        .replace("{repo}", repo)
        .replace("{out}", out)
}

// ====== output_of ======

pub fn output_of(fields: &[String]) -> Option<String> {
    let path = fields
        .iter()
        .find_map(|field| field.strip_prefix("/Fo").map(String::from))
        .or_else(|| {
            fields
                .windows(2)
                .find(|pair| pair[0] == "-o")
                .map(|pair| pair[1].clone())
        })?;
    path.rsplit('/').next().map(String::from)
}

// ====== resolved_plan ======

pub fn resolved_plan(context: &Context) -> Result<Vec<Vec<String>>, String> {
    let arguments = profile_arguments(&context.profile)?;
    let result = Command::new(&context.planner)
        .args(&arguments)
        .output()
        .map_err(|error| format!("{}: {error}", context.planner))?;
    if !result.status.success() {
        return Err(String::from_utf8_lossy(&result.stderr).trim().to_string());
    }

    let bin = match &context.toolchain {
        Some(toolchain) => normal(toolchain),
        None => newest_toolchain(Path::new(MSVC_ROOT))?,
    };
    let sdk = match &context.sdk {
        Some(sdk) => normal(sdk),
        None => newest_sdk(Path::new(SDK_ROOT))?,
    };
    let repo = normal(&context.repo);
    let out = normal(&context.out);

    let mut rows = Vec::new();
    for line in String::from_utf8_lossy(&result.stdout).lines() {
        let mut fields: Vec<String> = line
            .split('\t')
            .map(|field| resolve(field, &bin, &sdk, &repo, &out))
            .collect();
        let program = match fields[0].as_str() {
            "cl.exe" => format!("{bin}/cl.exe"),
            "ml64.exe" => format!("{bin}/ml64.exe"),
            "link.exe" => format!("{bin}/link.exe"),
            "rustc.exe" | "rustc" => normal(&context.rustc),
            "nasm.exe" => normal(&context.nasm),
            "convert-image.ps1" => fields[0].clone(),
            other => return Err(format!("Unexpected tool in Rust plan: {other}")),
        };
        fields[0] = program;
        rows.push(fields);
    }
    if rows.len() < 3
        || !rows[rows.len() - 1][0].ends_with("convert-image.ps1")
        || !rows[rows.len() - 2][0].ends_with("/link.exe")
    {
        return Err("Rust plan is missing the final link or conversion.".into());
    }
    Ok(rows)
}

// ====== meson_rows ======

pub fn meson_rows(rows: &[Vec<String>], link_hash: &str, out: &str) -> Result<Vec<String>, String> {
    let template_include = format!("-I{}/", normal(out));
    let mut lines = Vec::new();
    for row in rows {
        if row[0].ends_with("convert-image.ps1") {
            continue;
        }
        if row[0].ends_with("/link.exe") {
            lines.push(format!("link\tkernel.exe\t{link_hash}"));
            continue;
        }
        let output = output_of(row).ok_or("Compiler command has no output.")?;
        let kind = if row.contains(&template_include) {
            "after-template"
        } else if row.iter().any(|field| field == "--extern") {
            "after-lib"
        } else {
            "compile"
        };
        lines.push(format!("{kind}\t{output}\t{output}.d\t{}", row.join("\t")));
    }
    Ok(lines)
}

// ====== audit_line ======

pub fn audit_line(row: &[String]) -> String {
    let program = row[0].rsplit('/').next().unwrap_or(&row[0]);
    let mut line = program.to_string();
    for field in &row[1..] {
        line.push('\t');
        line.push_str(field);
    }
    line
}

// ====== remove_stale_audits ======

pub fn remove_stale_audits(out: &str, rows: &[Vec<String>]) -> Result<(), String> {
    let mut expected: Vec<String> = rows
        .iter()
        .filter_map(|row| output_of(row))
        .map(|output| format!("{output}{AUDIT_SUFFIX}"))
        .collect();
    expected.push(format!("kernel.exe{AUDIT_SUFFIX}"));
    expected.push(format!("kernel.elf{AUDIT_SUFFIX}"));
    for entry in fs::read_dir(out).map_err(|error| format!("{out}: {error}"))? {
        let entry = entry.map_err(|error| error.to_string())?;
        let name = entry.file_name().to_string_lossy().to_string();
        if name.ends_with(AUDIT_SUFFIX) && !expected.contains(&name) {
            fs::remove_file(entry.path()).map_err(|error| error.to_string())?;
        }
    }
    Ok(())
}
