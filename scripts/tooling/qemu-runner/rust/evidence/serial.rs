use std::collections::BTreeMap;
use std::path::Path;

const FAILURE_PLAIN: [&str; 4] = [
    "KERNEL_EXCEPTION_HALTED",
    "DISPATCH_FAILED",
    "PS2_INPUT_FAILED",
    "KERNEL_CONSOLE_DRAW_REJECTED_PM",
];

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Event {
    pub line: usize,
    pub level: String,
    pub name: String,
}

#[derive(Clone, Debug, Default)]
pub struct SerialLog {
    pub text: String,
    pub events: Vec<Event>,
    pub fields: BTreeMap<String, Vec<u64>>,
}

// ====== python_space ======

fn python_space(c: char) -> bool {
    matches!(c, '\u{1c}'..='\u{1f}') || c.is_whitespace()
}

// ====== word_char ======

fn word_char(c: char) -> bool {
    c == '_' || c.is_alphanumeric()
}

// ====== strip_ansi ======

pub fn strip_ansi(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut output = Vec::with_capacity(bytes.len());
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] == 0x1b && bytes.get(index + 1) == Some(&b'[') {
            let mut cursor = index + 2;
            while matches!(bytes.get(cursor), Some(0x30..=0x3f)) {
                cursor += 1;
            }
            while matches!(bytes.get(cursor), Some(0x20..=0x2f)) {
                cursor += 1;
            }
            if matches!(bytes.get(cursor), Some(0x40..=0x7e)) {
                index = cursor + 1;
                continue;
            }
        }
        output.push(bytes[index]);
        index += 1;
    }
    String::from_utf8_lossy(&output).into_owned()
}

// ====== split_lines ======

pub fn split_lines(text: &str) -> Vec<&str> {
    let mut lines = Vec::new();
    let mut start = 0;
    let mut characters = text.char_indices().peekable();
    while let Some((index, c)) = characters.next() {
        let breaks = matches!(
            c,
            '\n' | '\r' | '\u{b}' | '\u{c}' | '\u{1c}' | '\u{1d}' | '\u{1e}' | '\u{85}' | '\u{2028}' | '\u{2029}'
        );
        if !breaks {
            continue;
        }
        lines.push(&text[start..index]);
        if c == '\r' && matches!(characters.peek(), Some((_, '\n'))) {
            characters.next();
        }
        start = characters.peek().map_or(text.len(), |(next, _)| *next);
    }
    if start < text.len() {
        lines.push(&text[start..]);
    }
    lines
}

// ====== parse_marker ======

fn parse_marker(line: &str) -> Option<(String, String)> {
    let rest = line.strip_prefix('[')?.trim_start_matches(python_space);
    let (level, rest) = ["OK", "INFO", "WARN", "FAIL"]
        .iter()
        .find_map(|level| rest.strip_prefix(level).map(|tail| (*level, tail)))?;
    let rest = rest.trim_start_matches(python_space).strip_prefix(']')?;
    let name = rest.trim_start_matches(python_space);
    if name.len() == rest.len() || name.is_empty() {
        return None;
    }
    let valid = name.bytes().all(|b| b.is_ascii_uppercase() || b.is_ascii_digit() || b == b'_');
    valid.then(|| (level.to_string(), name.to_string()))
}

// ====== parse_field ======

fn parse_field(line: &str) -> Option<(String, u64)> {
    let (name, rest) = line.split_once(": 0x")?;
    let valid = !name.is_empty()
        && name.bytes().all(|b| b.is_ascii_uppercase() || b.is_ascii_digit() || b == b'_');
    let digits = rest.as_bytes().get(..16)?;
    if !valid || !digits.iter().all(u8::is_ascii_hexdigit) {
        return None;
    }
    if let Some(next) = rest.as_bytes().get(16) {
        if next.is_ascii_hexdigit() || *next == b'x' {
            return None;
        }
    }
    let value = u64::from_str_radix(&rest[..16], 16).ok()?;
    Some((name.to_string(), value))
}

// ====== has_word ======

fn has_word(text: &str, word: &str, optional_tail: &str) -> bool {
    text.match_indices(word).any(|(index, _)| {
        let boundary_before = !text[..index].chars().next_back().is_some_and(word_char);
        let rest = &text[index + word.len()..];
        let ends = |tail: &str| !tail.chars().next().is_some_and(word_char);
        let with_tail = !optional_tail.is_empty()
            && rest.strip_prefix(optional_tail).is_some_and(|tail| ends(tail));
        boundary_before && (ends(rest) || with_tail)
    })
}

// ====== SerialLog ======

impl SerialLog {
    pub fn parse(raw: &[u8]) -> SerialLog {
        let decoded = String::from_utf8_lossy(raw).replace("\r\n", "\n").replace('\r', "\n");
        let text = strip_ansi(&decoded);
        let mut log = SerialLog::default();
        for (index, line) in split_lines(&text).iter().enumerate() {
            let line = line.trim_matches(python_space);
            if let Some((level, name)) = parse_marker(line) {
                log.events.push(Event { line: index, level, name });
                continue;
            }
            if let Some((name, value)) = parse_field(line) {
                log.fields.entry(name).or_default().push(value);
            }
        }
        log.text = text;
        log
    }

    pub fn from_file(path: &Path) -> SerialLog {
        std::fs::read(path).map_or_else(|_| SerialLog::default(), |raw| SerialLog::parse(&raw))
    }

    pub fn count(&self, name: &str, level: Option<&str>) -> usize {
        self.events
            .iter()
            .filter(|event| event.name == name && level.is_none_or(|kind| event.level == kind))
            .count()
    }

    pub fn has(&self, name: &str) -> bool {
        self.count(name, None) > 0
    }

    pub fn before(&self, first: &str, second: &str) -> bool {
        let line = |name: &str| self.events.iter().find(|event| event.name == name).map(|event| event.line);
        matches!((line(first), line(second)), (Some(a), Some(b)) if a < b)
    }

    pub fn failures(&self) -> Vec<&str> {
        self.events.iter().filter(|event| event.level == "FAIL").map(|event| event.name.as_str()).collect()
    }

    pub fn terminal(&self) -> bool {
        FAILURE_PLAIN.iter().any(|name| self.text.contains(name))
            || has_word(&self.text, "KERNEL_SMP_PANIC", "_HALTED")
            || has_word(&self.text, "PANIC", "")
    }

    pub fn number(&self, field: &str, index: usize) -> Result<u64, String> {
        self.fields
            .get(field)
            .and_then(|values| values.get(index))
            .copied()
            .ok_or_else(|| format!("Missing numeric evidence: {field}"))
    }

    pub fn numbers(&self, field: &str) -> &[u64] {
        self.fields.get(field).map_or(&[], Vec::as_slice)
    }
}
