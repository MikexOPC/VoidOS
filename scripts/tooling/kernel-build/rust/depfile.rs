// ====== parse_source_dependencies ======

pub fn parse_source_dependencies(text: &str) -> Result<Vec<String>, String> {
    const MALFORMED: &str = "Malformed compiler dependency file.";
    let key = text.find("\"Includes\"").ok_or(MALFORMED)?;
    let open = text[key..].find('[').ok_or(MALFORMED)? + key + 1;
    let mut paths = Vec::new();
    let mut characters = text[open..].chars();
    while let Some(character) = characters.next() {
        match character {
            ']' => return Ok(paths),
            '"' => {
                let mut path = String::new();
                loop {
                    match characters.next().ok_or(MALFORMED)? {
                        '"' => break,
                        '\\' => path.push(characters.next().ok_or(MALFORMED)?),
                        other => path.push(other),
                    }
                }
                paths.push(path);
            }
            _ => {}
        }
    }
    Err(MALFORMED.into())
}

// ====== parse_prerequisites ======

pub fn parse_prerequisites(text: &str) -> Vec<String> {
    let mut paths = Vec::new();
    let joined = text.replace("\\\r\n", " ").replace("\\\n", " ");
    for line in joined.lines() {
        if line.trim_start().starts_with('#') {
            continue;
        }
        let tokens = tokens(line);
        let separator = tokens
            .iter()
            .position(|token| token == ":" || (token.ends_with(':') && token.len() > 2));
        let Some(separator) = separator else {
            continue;
        };
        paths.extend(tokens[separator + 1..].iter().cloned());
    }
    paths
}

// ====== tokens ======

fn tokens(line: &str) -> Vec<String> {
    let mut found = Vec::new();
    let mut current = String::new();
    let mut characters = line.chars().peekable();
    while let Some(character) = characters.next() {
        match character {
            '\\' if characters.peek() == Some(&' ') => {
                current.push(' ');
                characters.next();
            }
            ' ' | '\t' => {
                if !current.is_empty() {
                    found.push(std::mem::take(&mut current));
                }
            }
            _ => current.push(character),
        }
    }
    if !current.is_empty() {
        found.push(current);
    }
    found
}

// ====== render ======

pub fn render(output: &str, prerequisites: &[String]) -> String {
    let mut seen = std::collections::BTreeSet::new();
    let mut text = format!("{}:", escape(output));
    for path in prerequisites {
        let normal = path.replace('\\', "/");
        if normal != output && !normal.ends_with(&format!("/{output}")) && seen.insert(normal.clone()) {
            text.push_str(" \\\n ");
            text.push_str(&escape(&normal));
        }
    }
    text.push('\n');
    text
}

// ====== escape ======

fn escape(path: &str) -> String {
    path.replace(' ', "\\ ")
}

// ====== declares_include ======

pub fn declares_include(source: &str) -> bool {
    source.lines().any(|line| {
        let word = line.trim_start().split_whitespace().next().unwrap_or("");
        word.eq_ignore_ascii_case("include") || word.eq_ignore_ascii_case("includelib")
    })
}
