// ====== mini-CSS theme and dock configuration (/etc/ui/theme.vss) ======

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DockEdge {
    Bottom,
    Top,
    Left,
    Right,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DockShape {
    Floating,
    Bar,
    Compact,
}

// Colors are stored in native 32-bit BGRA (UEFI GOP PixelBlueGreenRedReserved8BitPerColor + alpha in bits 31..24).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Theme {
    pub bg_top: u32,
    pub bg_bottom: u32,
    pub window_bg: u32,
    pub window_border: u32,
    pub window_border_focus: u32,
    pub titlebar_bg: u32,
    pub titlebar_focus: u32,
    pub text_primary: u32,
    pub text_muted: u32,
    pub accent: u32,
    pub accent_hover: u32,
    pub danger: u32,
    pub dock_bg: u32,
    pub dock_border: u32,
    pub dock_edge: DockEdge,
    pub dock_shape: DockShape,
    pub dock_radius: u32,
    pub dock_thickness: u32,
    pub window_radius: u32,
    pub anim_ms: u32,
}

// ====== rgb / rgba helpers ======

pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> u32 {
    (b as u32) | ((g as u32) << 8) | ((r as u32) << 16) | ((a as u32) << 24)
}

pub const fn rgb(r: u8, g: u8, b: u8) -> u32 {
    rgba(r, g, b, 0xFF)
}

impl Theme {
    pub const fn void_dark() -> Self {
        Self {
            bg_top: rgb(0x0D, 0x11, 0x1A),
            bg_bottom: rgb(0x16, 0x1E, 0x2E),
            window_bg: rgb(0x15, 0x1A, 0x24),
            window_border: rgb(0x2A, 0x33, 0x44),
            window_border_focus: rgb(0x3B, 0x82, 0xF6),
            titlebar_bg: rgb(0x1B, 0x22, 0x30),
            titlebar_focus: rgb(0x21, 0x2A, 0x3C),
            text_primary: rgb(0xE8, 0xEE, 0xF7),
            text_muted: rgb(0x8D, 0x99, 0xAE),
            accent: rgb(0x3B, 0x82, 0xF6),
            accent_hover: rgb(0x60, 0xA5, 0xFA),
            danger: rgb(0xEF, 0x44, 0x44),
            dock_bg: rgba(0x14, 0x19, 0x24, 0xD8),
            dock_border: rgba(0x40, 0x4E, 0x68, 0xE0),
            dock_edge: DockEdge::Bottom,
            dock_shape: DockShape::Floating,
            dock_radius: 14,
            dock_thickness: 48,
            window_radius: 8,
            anim_ms: 180,
        }
    }

    pub fn parse_vss(input: &[u8]) -> Self {
        let mut theme = Self::void_dark();
        let mut idx = 0usize;
        let len = input.len();
        while idx < len {
            skip_ws_and_comments(input, &mut idx);
            if idx >= len {
                break;
            }
            let sel_start = idx;
            while idx < len && input[idx] != b'{' && !is_ws(input[idx]) {
                idx += 1;
            }
            let selector = &input[sel_start..idx];
            skip_ws_and_comments(input, &mut idx);
            if idx >= len || input[idx] != b'{' {
                // Skip malformed token to next '}' or EOF.
                while idx < len && input[idx] != b'}' {
                    idx += 1;
                }
                if idx < len {
                    idx += 1;
                }
                continue;
            }
            idx += 1; // consume '{'
            while idx < len {
                skip_ws_and_comments(input, &mut idx);
                if idx >= len || input[idx] == b'}' {
                    if idx < len {
                        idx += 1;
                    }
                    break;
                }
                let key_start = idx;
                while idx < len && input[idx] != b':' && input[idx] != b'}' && input[idx] != b';' {
                    idx += 1;
                }
                let key = trim_ascii(&input[key_start..idx]);
                if idx >= len || input[idx] != b':' {
                    while idx < len && input[idx] != b';' && input[idx] != b'}' {
                        idx += 1;
                    }
                    if idx < len && input[idx] == b';' {
                        idx += 1;
                    }
                    continue;
                }
                idx += 1; // consume ':'
                let val_start = idx;
                while idx < len && input[idx] != b';' && input[idx] != b'}' {
                    idx += 1;
                }
                let val = trim_ascii(&input[val_start..idx]);
                if idx < len && input[idx] == b';' {
                    idx += 1;
                }
                apply_rule(&mut theme, selector, key, val);
            }
        }
        theme
    }
}

// ====== parser helpers ======

#[inline]
fn is_ws(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\r' | b'\n')
}

fn trim_ascii(mut s: &[u8]) -> &[u8] {
    while let Some(&first) = s.first() {
        if is_ws(first) {
            s = &s[1..];
        } else {
            break;
        }
    }
    while let Some(&last) = s.last() {
        if is_ws(last) {
            s = &s[..s.len() - 1];
        } else {
            break;
        }
    }
    s
}

fn skip_ws_and_comments(input: &[u8], idx: &mut usize) {
    let len = input.len();
    while *idx < len {
        if is_ws(input[*idx]) {
            *idx += 1;
            continue;
        }
        if *idx + 1 < len && input[*idx] == b'/' && input[*idx + 1] == b'*' {
            *idx += 2;
            while *idx + 1 < len && !(input[*idx] == b'*' && input[*idx + 1] == b'/') {
                *idx += 1;
            }
            if *idx + 1 < len {
                *idx += 2;
            } else {
                *idx = len;
            }
            continue;
        }
        if *idx + 1 < len && input[*idx] == b'/' && input[*idx + 1] == b'/' {
            *idx += 2;
            while *idx < len && input[*idx] != b'\n' {
                *idx += 1;
            }
            continue;
        }
        break;
    }
}

fn hex_nibble(b: u8) -> Option<u32> {
    match b {
        b'0'..=b'9' => Some((b - b'0') as u32),
        b'a'..=b'f' => Some((b - b'a' + 10) as u32),
        b'A'..=b'F' => Some((b - b'A' + 10) as u32),
        _ => None,
    }
}

pub fn parse_hex_color(val: &[u8]) -> Option<u32> {
    if val.first().copied() != Some(b'#') {
        return None;
    }
    let hex = &val[1..];
    if hex.len() != 6 && hex.len() != 8 {
        return None;
    }
    let mut digits = [0u32; 8];
    for (i, &b) in hex.iter().enumerate() {
        digits[i] = hex_nibble(b)?;
    }
    let r = ((digits[0] << 4) | digits[1]) as u8;
    let g = ((digits[2] << 4) | digits[3]) as u8;
    let b = ((digits[4] << 4) | digits[5]) as u8;
    let a = if hex.len() == 8 {
        ((digits[6] << 4) | digits[7]) as u8
    } else {
        0xFF
    };
    Some(rgba(r, g, b, a))
}

fn parse_u32_prefix(val: &[u8]) -> Option<u32> {
    let mut acc: u32 = 0;
    let mut any = false;
    for &b in val {
        if b.is_ascii_digit() {
            any = true;
            acc = acc.checked_mul(10)?.checked_add((b - b'0') as u32)?;
        } else {
            break;
        }
    }
    if any { Some(acc) } else { None }
}

fn apply_rule(theme: &mut Theme, selector: &[u8], key: &[u8], val: &[u8]) {
    match (selector, key) {
        (b"desktop", b"bg-top") | (b"desktop", b"background") => {
            if let Some(c) = parse_hex_color(val) {
                theme.bg_top = c;
            }
        }
        (b"desktop", b"bg-bottom") => {
            if let Some(c) = parse_hex_color(val) {
                theme.bg_bottom = c;
            }
        }
        (b"window", b"background") => {
            if let Some(c) = parse_hex_color(val) {
                theme.window_bg = c;
            }
        }
        (b"window", b"border") => {
            if let Some(c) = parse_hex_color(val) {
                theme.window_border = c;
            }
        }
        (b"window", b"border-focus") => {
            if let Some(c) = parse_hex_color(val) {
                theme.window_border_focus = c;
            }
        }
        (b"window", b"titlebar") => {
            if let Some(c) = parse_hex_color(val) {
                theme.titlebar_bg = c;
            }
        }
        (b"window", b"titlebar-focus") => {
            if let Some(c) = parse_hex_color(val) {
                theme.titlebar_focus = c;
            }
        }
        (b"window", b"radius") => {
            if let Some(n) = parse_u32_prefix(val) {
                theme.window_radius = n.min(24);
            }
        }
        (b"dock", b"background") => {
            if let Some(c) = parse_hex_color(val) {
                theme.dock_bg = c;
            }
        }
        (b"dock", b"border") => {
            if let Some(c) = parse_hex_color(val) {
                theme.dock_border = c;
            }
        }
        (b"dock", b"edge") => match val {
            b"bottom" => theme.dock_edge = DockEdge::Bottom,
            b"top" => theme.dock_edge = DockEdge::Top,
            b"left" => theme.dock_edge = DockEdge::Left,
            b"right" => theme.dock_edge = DockEdge::Right,
            _ => {}
        },
        (b"dock", b"shape") => match val {
            b"floating" => theme.dock_shape = DockShape::Floating,
            b"bar" => theme.dock_shape = DockShape::Bar,
            b"compact" => theme.dock_shape = DockShape::Compact,
            _ => {}
        },
        (b"dock", b"radius") => {
            if let Some(n) = parse_u32_prefix(val) {
                theme.dock_radius = n.min(32);
            }
        }
        (b"dock", b"thickness") | (b"dock", b"height") => {
            if let Some(n) = parse_u32_prefix(val) {
                theme.dock_thickness = n.clamp(28, 96);
            }
        }
        (b"dock", b"transition") | (b"window", b"transition") | (b"desktop", b"transition") => {
            if let Some(n) = parse_u32_prefix(val) {
                theme.anim_ms = n.clamp(0, 1000);
            }
        }
        (b"theme", b"accent") => {
            if let Some(c) = parse_hex_color(val) {
                theme.accent = c;
            }
        }
        (b"theme", b"accent-hover") => {
            if let Some(c) = parse_hex_color(val) {
                theme.accent_hover = c;
            }
        }
        (b"theme", b"text") => {
            if let Some(c) = parse_hex_color(val) {
                theme.text_primary = c;
            }
        }
        (b"theme", b"text-muted") => {
            if let Some(c) = parse_hex_color(val) {
                theme.text_muted = c;
            }
        }
        _ => {}
    }
}
