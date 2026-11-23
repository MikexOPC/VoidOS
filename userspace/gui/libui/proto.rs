// ====== language-neutral #[repr(C)] IPC protocol between compositor and apps ======

pub const PROTO_MAGIC: u32 = 0x564F_4944; // "VOID"

pub const MSG_CREATE_WINDOW: u32 = 1;
pub const MSG_WINDOW_CREATED: u32 = 2;
pub const MSG_COMMIT_RECT: u32 = 3;
pub const MSG_CLOSE_WINDOW: u32 = 4;
pub const MSG_INPUT_KEY: u32 = 5;
pub const MSG_INPUT_MOUSE: u32 = 6;
pub const MSG_FOCUS_CHANGED: u32 = 7;
pub const MSG_DOCK_SET_MODE: u32 = 8;

pub const MAX_TITLE_BYTES: usize = 48;
pub const PROTO_WIRE_BYTES: usize = 96;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GuiMessage {
    pub magic: u32,
    pub kind: u32,
    pub win_id: u32,
    pub flags: u32,
    pub a: u64,
    pub b: u64,
    pub c: i32,
    pub d: i32,
    pub e: u32,
    pub f: u32,
    pub title: [u8; MAX_TITLE_BYTES],
}

const _: () = assert!(core::mem::size_of::<GuiMessage>() == PROTO_WIRE_BYTES);

impl GuiMessage {
    pub const fn new(kind: u32, win_id: u32) -> Self {
        Self {
            magic: PROTO_MAGIC,
            kind,
            win_id,
            flags: 0,
            a: 0,
            b: 0,
            c: 0,
            d: 0,
            e: 0,
            f: 0,
            title: [0u8; MAX_TITLE_BYTES],
        }
    }

    pub fn create_window(width: u32, height: u32, shm_id: u64, title: &[u8]) -> Self {
        let mut msg = Self::new(MSG_CREATE_WINDOW, 0);
        msg.a = shm_id;
        msg.e = width;
        msg.f = height;
        let n = title.len().min(MAX_TITLE_BYTES - 1);
        msg.title[..n].copy_from_slice(&title[..n]);
        msg
    }

    pub fn commit_rect(win_id: u32, x: u32, y: u32, width: u32, height: u32) -> Self {
        let mut msg = Self::new(MSG_COMMIT_RECT, win_id);
        msg.c = x as i32;
        msg.d = y as i32;
        msg.e = width;
        msg.f = height;
        msg
    }

    pub fn input_key(win_id: u32, code: u32, scalar: u32, mods: u32) -> Self {
        let mut msg = Self::new(MSG_INPUT_KEY, win_id);
        msg.flags = mods;
        msg.e = code;
        msg.f = scalar;
        msg
    }

    pub fn input_mouse(win_id: u32, x: i32, y: i32, buttons: u32) -> Self {
        let mut msg = Self::new(MSG_INPUT_MOUSE, win_id);
        msg.flags = buttons;
        msg.c = x;
        msg.d = y;
        msg
    }

    pub fn encode(&self) -> [u8; PROTO_WIRE_BYTES] {
        let mut out = [0u8; PROTO_WIRE_BYTES];
        out[0..4].copy_from_slice(&self.magic.to_le_bytes());
        out[4..8].copy_from_slice(&self.kind.to_le_bytes());
        out[8..12].copy_from_slice(&self.win_id.to_le_bytes());
        out[12..16].copy_from_slice(&self.flags.to_le_bytes());
        out[16..24].copy_from_slice(&self.a.to_le_bytes());
        out[24..32].copy_from_slice(&self.b.to_le_bytes());
        out[32..36].copy_from_slice(&self.c.to_le_bytes());
        out[36..40].copy_from_slice(&self.d.to_le_bytes());
        out[40..44].copy_from_slice(&self.e.to_le_bytes());
        out[44..48].copy_from_slice(&self.f.to_le_bytes());
        out[48..96].copy_from_slice(&self.title);
        out
    }

    pub fn decode(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < PROTO_WIRE_BYTES {
            return None;
        }
        let magic = u32::from_le_bytes(bytes[0..4].try_into().ok()?);
        if magic != PROTO_MAGIC {
            return None;
        }
        let mut title = [0u8; MAX_TITLE_BYTES];
        title.copy_from_slice(&bytes[48..96]);
        Some(Self {
            magic,
            kind: u32::from_le_bytes(bytes[4..8].try_into().ok()?),
            win_id: u32::from_le_bytes(bytes[8..12].try_into().ok()?),
            flags: u32::from_le_bytes(bytes[12..16].try_into().ok()?),
            a: u64::from_le_bytes(bytes[16..24].try_into().ok()?),
            b: u64::from_le_bytes(bytes[24..32].try_into().ok()?),
            c: i32::from_le_bytes(bytes[32..36].try_into().ok()?),
            d: i32::from_le_bytes(bytes[36..40].try_into().ok()?),
            e: u32::from_le_bytes(bytes[40..44].try_into().ok()?),
            f: u32::from_le_bytes(bytes[44..48].try_into().ok()?),
            title,
        })
    }
}
