use crate::plan;
use std::fs;
use std::path::Path;

// ====== compare ======

pub fn compare(expected: &[String], actual: &[String]) -> Result<(), String> {
    for (index, (left, right)) in expected.iter().zip(actual).enumerate() {
        if left != right {
            return Err(format!(
                "Plan mismatch at command {}:\n  plan:  {left}\n  meson: {right}",
                index + 1
            ));
        }
    }
    if expected.len() != actual.len() {
        return Err(format!(
            "Different command counts: plan {}, meson {}.",
            expected.len(),
            actual.len()
        ));
    }
    Ok(())
}

// ====== audit_name ======

fn audit_name(row: &[String]) -> Result<String, String> {
    if row[0].ends_with("/link.exe") {
        return Ok("kernel.exe".into());
    }
    if row[0].ends_with("convert-image.ps1") {
        return Ok("kernel.elf".into());
    }
    plan::output_of(row).ok_or_else(|| "Compiler command has no output.".to_string())
}

// ====== check ======

pub fn check(context: &plan::Context) -> Result<usize, String> {
    let rows = plan::resolved_plan(context)?;
    let out = Path::new(&context.out);
    let mut expected = Vec::new();
    let mut actual = Vec::new();
    for row in &rows {
        expected.push(plan::audit_line(row));
        let path = out.join(format!("{}{}", audit_name(row)?, plan::AUDIT_SUFFIX));
        let text = fs::read_to_string(&path).map_err(|error| format!("{}: {error}", path.display()))?;
        actual.push(text.trim_end().to_string());
    }
    compare(&expected, &actual)?;

    let audits = fs::read_dir(out)
        .map_err(|error| error.to_string())?
        .filter_map(|entry| entry.ok())
        .filter(|entry| entry.file_name().to_string_lossy().ends_with(plan::AUDIT_SUFFIX))
        .count();
    if audits != rows.len() {
        return Err(format!("Meson ran {audits} commands outside a plan of {}.", rows.len()));
    }
    Ok(rows.len())
}
