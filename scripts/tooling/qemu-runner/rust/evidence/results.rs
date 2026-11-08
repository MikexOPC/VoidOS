use std::fmt::Write;

#[derive(Clone, Debug, PartialEq)]
pub enum Value {
    Bool(bool),
    Number(u64),
    Text(String),
    List(Vec<Value>),
    Object(Vec<(String, Value)>),
}

// ====== object ======

pub fn object(items: Vec<(&str, Value)>) -> Value {
    Value::Object(items.into_iter().map(|(key, value)| (key.to_string(), value)).collect())
}

// ====== write_text ======

fn write_text(output: &mut String, text: &str) {
    output.push('"');
    for c in text.chars() {
        match c {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            '\u{8}' => output.push_str("\\b"),
            '\u{c}' => output.push_str("\\f"),
            ' '..='~' => output.push(c),
            _ => {
                let mut units = [0u16; 2];
                for unit in c.encode_utf16(&mut units) {
                    let _ = write!(output, "\\u{unit:04x}");
                }
            }
        }
    }
    output.push('"');
}

// ====== write_items ======

fn write_items<T>(
    output: &mut String,
    depth: usize,
    brackets: (char, char),
    items: &[T],
    mut item: impl FnMut(&mut String, &T),
) {
    output.push(brackets.0);
    if items.is_empty() {
        output.push(brackets.1);
        return;
    }
    for (index, value) in items.iter().enumerate() {
        output.push_str(if index == 0 { "\n" } else { ",\n" });
        output.push_str(&"  ".repeat(depth + 1));
        item(output, value);
    }
    output.push('\n');
    output.push_str(&"  ".repeat(depth));
    output.push(brackets.1);
}

// ====== write_value ======

fn write_value(output: &mut String, value: &Value, depth: usize) {
    match value {
        Value::Bool(flag) => output.push_str(if *flag { "true" } else { "false" }),
        Value::Number(number) => {
            let _ = write!(output, "{number}");
        }
        Value::Text(text) => write_text(output, text),
        Value::List(items) => write_items(output, depth, ('[', ']'), items, |output, item| {
            write_value(output, item, depth + 1);
        }),
        Value::Object(items) => write_items(output, depth, ('{', '}'), items, |output, (key, item)| {
            write_text(output, key);
            output.push_str(": ");
            write_value(output, item, depth + 1);
        }),
    }
}

// ====== render ======

pub fn render(value: &Value) -> String {
    let mut output = String::new();
    write_value(&mut output, value, 0);
    output
}
