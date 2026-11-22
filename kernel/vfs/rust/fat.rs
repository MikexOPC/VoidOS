#![cfg_attr(not(test), no_std)]

// ====== fat16_fat32_filesystem_policy ======

// Microsoft EFI FAT32 File System Specification v1.03 / UEFI Specification §13.3:
// MBR partition detection, FAT16/FAT32 BPB validation, cluster-chain bounds and
// directory entry (8.3 + VFAT LFN) parsing without panicking slice indexing.

pub const KFAT_OK: u32 = 0;
pub const KFAT_ERR_CORRUPT: u32 = 1;
pub const KFAT_ERR_NOT_FOUND: u32 = 2;
pub const KFAT_ERR_EOF: u32 = 3;

pub const FAT_CLUSTER_EOF: u32 = 0x0FFF_FFFF;
pub const FAT_CLUSTER_BAD: u32 = 0x0FFF_FFF7;

pub const FAT_ATTR_READ_ONLY: u8 = 0x01;
pub const FAT_ATTR_HIDDEN: u8 = 0x02;
pub const FAT_ATTR_SYSTEM: u8 = 0x04;
pub const FAT_ATTR_VOLUME_ID: u8 = 0x08;
pub const FAT_ATTR_DIRECTORY: u8 = 0x10;
pub const FAT_ATTR_ARCHIVE: u8 = 0x20;
pub const FAT_ATTR_LONG_NAME: u8 = 0x0F;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FatGeometry {
    pub partition_lba: u64,
    pub total_sectors: u64,
    pub fat_start_lba: u64,
    pub fat_sectors: u32,
    pub num_fats: u32,
    pub root_dir_lba: u64,
    pub root_dir_sectors: u32,
    pub root_dir_entries: u32,
    pub data_start_lba: u64,
    pub total_clusters: u32,
    pub sectors_per_cluster: u32,
    pub root_cluster: u32,
    pub is_fat32: u32,
    pub reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FatDirEntry {
    pub name: [u8; 64],
    pub name_len: u32,
    pub attr: u32,
    pub first_cluster: u32,
    pub file_size: u32,
    pub entry_offset: u32,
}

impl Default for FatDirEntry {
    fn default() -> Self {
        Self {
            name: [0u8; 64],
            name_len: 0,
            attr: 0,
            first_cluster: 0,
            file_size: 0,
            entry_offset: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FatLfnState {
    pub buffer: [u8; 64],
    pub active: u32,
}

impl Default for FatLfnState {
    fn default() -> Self {
        Self {
            buffer: [0u8; 64],
            active: 0,
        }
    }
}

// ====== byte_at ======

#[inline(always)]
fn byte_at(bytes: &[u8], offset: usize) -> u8 {
    match bytes.get(offset) {
        Some(&b) => b,
        None => 0,
    }
}

// ====== read_le16 ======

fn read_le16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([byte_at(bytes, offset), byte_at(bytes, offset + 1)])
}

// ====== read_le32 ======

fn read_le32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        byte_at(bytes, offset),
        byte_at(bytes, offset + 1),
        byte_at(bytes, offset + 2),
        byte_at(bytes, offset + 3),
    ])
}

// ====== looks_like_bpb ======

fn looks_like_bpb(sector: &[u8]) -> bool {
    if sector.len() < 512 || byte_at(sector, 510) != 0x55 || byte_at(sector, 511) != 0xAA {
        return false;
    }
    let jmp0 = byte_at(sector, 0);
    if jmp0 != 0xEB && jmp0 != 0xE9 && jmp0 != 0xE8 {
        return false;
    }
    let bps = read_le16(sector, 11);
    let spc = byte_at(sector, 13);
    let rsvd = read_le16(sector, 14);
    let fats = byte_at(sector, 16);
    bps == 512 && spc != 0 && (spc & (spc - 1)) == 0 && rsvd >= 1 && (fats == 1 || fats == 2)
}

// ====== detect_partition_slice ======

pub fn detect_partition_slice(sector0: &[u8], disk_sectors: u64) -> Result<u64, u32> {
    if sector0.len() != 512 || disk_sectors == 0 {
        return Err(KFAT_ERR_CORRUPT);
    }
    if byte_at(sector0, 510) != 0x55 || byte_at(sector0, 511) != 0xAA {
        return Err(KFAT_ERR_CORRUPT);
    }

    // Check if an MBR partition entry at 446..510 points to a valid non-zero LBA inside the disk.
    let mut part = 0usize;
    while part < 4 {
        let base = 446 + part * 16;
        let status = byte_at(sector0, base);
        let ptype = byte_at(sector0, base + 4);
        let first_lba = read_le32(sector0, base + 8) as u64;
        let count = read_le32(sector0, base + 12) as u64;

        let valid_status = status == 0x00 || status == 0x80;
        let is_fat_type = matches!(ptype, 0x01 | 0x04 | 0x06 | 0x0B | 0x0C | 0x0E | 0xEF);
        if valid_status
            && is_fat_type
            && first_lba > 0
            && count > 0
            && first_lba < disk_sectors
            && count <= disk_sectors - first_lba
        {
            return Ok(first_lba);
        }
        part += 1;
    }

    if looks_like_bpb(sector0) {
        return Ok(0);
    }
    Err(KFAT_ERR_CORRUPT)
}

// ====== parse_bpb_slice ======

pub fn parse_bpb_slice(
    bpb: &[u8],
    partition_lba: u64,
    disk_sectors: u64,
) -> Result<FatGeometry, u32> {
    if !looks_like_bpb(bpb) || partition_lba >= disk_sectors {
        return Err(KFAT_ERR_CORRUPT);
    }

    let bytes_per_sec = read_le16(bpb, 11) as u32;
    let sec_per_clus = byte_at(bpb, 13) as u32;
    let rsvd_sec_cnt = read_le16(bpb, 14) as u32;
    let num_fats = byte_at(bpb, 16) as u32;
    let root_ent_cnt = read_le16(bpb, 17) as u32;
    let tot_sec16 = read_le16(bpb, 19) as u64;
    let fat_sz16 = read_le16(bpb, 22) as u32;
    let tot_sec32 = read_le32(bpb, 32) as u64;

    let total_sectors = if tot_sec16 != 0 { tot_sec16 } else { tot_sec32 };
    if total_sectors == 0 || total_sectors > disk_sectors - partition_lba {
        return Err(KFAT_ERR_CORRUPT);
    }

    let is_fat32 = if fat_sz16 == 0 && root_ent_cnt == 0 {
        1u32
    } else if fat_sz16 != 0 && root_ent_cnt != 0 && (root_ent_cnt & 15) == 0 {
        0u32
    } else {
        return Err(KFAT_ERR_CORRUPT);
    };

    let fat_sectors = if is_fat32 != 0 {
        read_le32(bpb, 36)
    } else {
        fat_sz16
    };
    if fat_sectors == 0 {
        return Err(KFAT_ERR_CORRUPT);
    }

    let root_dir_sectors = (root_ent_cnt * 32).div_ceil(bytes_per_sec);
    let fat_total = (num_fats as u64).saturating_mul(fat_sectors as u64);
    let overhead = (rsvd_sec_cnt as u64)
        .saturating_add(fat_total)
        .saturating_add(root_dir_sectors as u64);
    if overhead >= total_sectors {
        return Err(KFAT_ERR_CORRUPT);
    }

    let data_sectors = total_sectors - overhead;
    let total_clusters_u64 = data_sectors / (sec_per_clus as u64);
    if total_clusters_u64 == 0 || total_clusters_u64 > 0x0FFF_FFF0 {
        return Err(KFAT_ERR_CORRUPT);
    }
    let total_clusters = total_clusters_u64 as u32;

    let root_cluster = if is_fat32 != 0 {
        let rc = read_le32(bpb, 44);
        if rc < 2 || rc >= total_clusters + 2 {
            return Err(KFAT_ERR_CORRUPT);
        }
        rc
    } else {
        0
    };

    let fat_start_lba = partition_lba + (rsvd_sec_cnt as u64);
    let root_dir_lba = fat_start_lba + fat_total;
    let data_start_lba = root_dir_lba + (root_dir_sectors as u64);

    Ok(FatGeometry {
        partition_lba,
        total_sectors,
        fat_start_lba,
        fat_sectors,
        num_fats,
        root_dir_lba,
        root_dir_sectors,
        root_dir_entries: root_ent_cnt,
        data_start_lba,
        total_clusters,
        sectors_per_cluster: sec_per_clus,
        root_cluster,
        is_fat32,
        reserved: 0,
    })
}

// ====== kfat_detect_partition_lba ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_detect_partition_lba(
    sector0: *const u8,
    disk_sectors: u64,
    lba_out: *mut u64,
) -> u32 {
    if sector0.is_null() || lba_out.is_null() {
        return KFAT_ERR_CORRUPT;
    }
    let slice = unsafe { core::slice::from_raw_parts(sector0, 512) };
    match detect_partition_slice(slice, disk_sectors) {
        Ok(lba) => {
            unsafe {
                *lba_out = lba;
            }
            KFAT_OK
        }
        Err(code) => code,
    }
}

// ====== kfat_parse_bpb ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_parse_bpb(
    bpb_sector: *const u8,
    partition_lba: u64,
    disk_sectors: u64,
    out_geom: *mut FatGeometry,
) -> u32 {
    if bpb_sector.is_null() || out_geom.is_null() {
        return KFAT_ERR_CORRUPT;
    }
    let slice = unsafe { core::slice::from_raw_parts(bpb_sector, 512) };
    match parse_bpb_slice(slice, partition_lba, disk_sectors) {
        Ok(geom) => {
            unsafe {
                *out_geom = geom;
            }
            KFAT_OK
        }
        Err(code) => code,
    }
}

// ====== kfat_cluster_to_lba ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_cluster_to_lba(geom: *const FatGeometry, cluster: u32) -> u64 {
    if geom.is_null() {
        return 0;
    }
    let g = unsafe { &*geom };
    if cluster < 2 || cluster >= g.total_clusters.saturating_add(2) {
        return 0;
    }
    g.data_start_lba
        .saturating_add(((cluster - 2) as u64).saturating_mul(g.sectors_per_cluster as u64))
}

// ====== kfat_fat_slot ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_fat_slot(
    geom: *const FatGeometry,
    cluster: u32,
    sector_lba_out: *mut u64,
    byte_offset_out: *mut u32,
) -> u32 {
    if geom.is_null() || sector_lba_out.is_null() || byte_offset_out.is_null() {
        return 0;
    }
    let g = unsafe { &*geom };
    if cluster < 2 || cluster >= g.total_clusters.saturating_add(2) {
        return 0;
    }
    let byte_index = if g.is_fat32 != 0 {
        (cluster as u64) * 4
    } else {
        (cluster as u64) * 2
    };
    let sec_offset = byte_index / 512;
    if sec_offset >= g.fat_sectors as u64 {
        return 0;
    }
    unsafe {
        *sector_lba_out = g.fat_start_lba + sec_offset;
        *byte_offset_out = (byte_index % 512) as u32;
    }
    1
}

// ====== kfat_decode_next_cluster ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_decode_next_cluster(
    geom: *const FatGeometry,
    raw_value: u32,
) -> u32 {
    if geom.is_null() {
        return FAT_CLUSTER_BAD;
    }
    let g = unsafe { &*geom };
    let max_valid = g.total_clusters.saturating_add(1);
    if g.is_fat32 != 0 {
        let val = raw_value & 0x0FFF_FFFF;
        if val == 0 {
            0
        } else if val >= 0x0FFF_FFF8 {
            FAT_CLUSTER_EOF
        } else if val == 0x0FFF_FFF7 || val < 2 || val > max_valid {
            FAT_CLUSTER_BAD
        } else {
            val
        }
    } else {
        let val = raw_value & 0xFFFF;
        if val == 0 {
            0
        } else if val >= 0xFFF8 {
            FAT_CLUSTER_EOF
        } else if val == 0xFFF7 || val < 2 || val > max_valid {
            FAT_CLUSTER_BAD
        } else {
            val
        }
    }
}

// ====== ascii_upper ======

#[inline(always)]
fn ascii_upper(b: u8) -> u8 {
    if b.is_ascii_lowercase() {
        b - 32
    } else {
        b
    }
}

// ====== names_equal_ci ======

pub fn names_equal_ci(a: &[u8], a_len: usize, b: &[u8], b_len: usize) -> bool {
    if a_len != b_len || a_len == 0 || a_len > 63 {
        return false;
    }
    let mut i = 0usize;
    while i < a_len {
        if ascii_upper(byte_at(a, i)) != ascii_upper(byte_at(b, i)) {
            return false;
        }
        i += 1;
    }
    true
}

// ====== format_short_name ======

fn format_short_name(entry32: &[u8], out: &mut [u8; 64]) -> usize {
    let mut len = 0usize;
    let mut i = 0usize;
    while i < 8 {
        let mut ch = byte_at(entry32, i);
        if i == 0 && ch == 0x05 {
            ch = 0xE5;
        }
        if ch == b' ' || ch == 0 {
            break;
        }
        if let Some(slot) = out.get_mut(len) {
            *slot = ch;
        }
        len += 1;
        i += 1;
    }

    let mut ext_len = 0usize;
    let mut j = 8usize;
    while j < 11 {
        let ch = byte_at(entry32, j);
        if ch == b' ' || ch == 0 {
            break;
        }
        if ext_len == 0 {
            if let Some(slot) = out.get_mut(len) {
                *slot = b'.';
            }
            len += 1;
        }
        if let Some(slot) = out.get_mut(len) {
            *slot = ch;
        }
        len += 1;
        ext_len += 1;
        j += 1;
    }
    if let Some(slot) = out.get_mut(len) {
        *slot = 0;
    }
    len
}

// ====== feed_lfn_entry ======

fn feed_lfn_entry(entry32: &[u8], lfn: &mut FatLfnState) {
    let ord = byte_at(entry32, 0);
    let seq = (ord & 0x1F) as usize;
    if seq == 0 || seq > 4 {
        lfn.active = 0;
        return;
    }
    if (ord & 0x40) != 0 {
        let mut k = 0usize;
        while k < 64 {
            if let Some(slot) = lfn.buffer.get_mut(k) {
                *slot = 0;
            }
            k += 1;
        }
        lfn.active = 1;
    } else if lfn.active == 0 {
        return;
    }

    let offsets: [usize; 13] = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
    let base_pos = (seq - 1) * 13;
    let mut idx = 0usize;
    while idx < 13 {
        let off = match offsets.get(idx) {
            Some(&o) => o,
            None => 0,
        };
        let ch = read_le16(entry32, off);
        let dst = base_pos + idx;
        if dst < 63 {
            if ch == 0x0000 || ch == 0xFFFF {
                if let Some(slot) = lfn.buffer.get_mut(dst) {
                    *slot = 0;
                }
                break;
            }
            let ascii = if ch < 0x80 { ch as u8 } else { b'_' };
            if let Some(slot) = lfn.buffer.get_mut(dst) {
                *slot = ascii;
            }
        }
        idx += 1;
    }
}

// ====== lfn_length ======

fn lfn_length(lfn: &FatLfnState) -> usize {
    if lfn.active == 0 {
        return 0;
    }
    let mut len = 0usize;
    while len < 63 {
        if byte_at(&lfn.buffer, len) == 0 {
            break;
        }
        len += 1;
    }
    len
}

// ====== kfat_dir_step ======

// Parses a single 32-byte directory slot at `entry_32`.
// Returns:
//   0 = end of directory (0x00)
//   1 = skipped (deleted 0xE5, volume label, dot/dotdot, or LFN piece consumed into `lfn`)
//   2 = valid file/directory decoded into `out_entry`
#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_dir_step(
    entry_32: *const u8,
    entry_offset: u32,
    lfn: *mut FatLfnState,
    out_entry: *mut FatDirEntry,
) -> u32 {
    if entry_32.is_null() || lfn.is_null() || out_entry.is_null() {
        return 0;
    }
    let raw = unsafe { core::slice::from_raw_parts(entry_32, 32) };
    let lfn_state = unsafe { &mut *lfn };
    let first = byte_at(raw, 0);

    if first == 0x00 {
        lfn_state.active = 0;
        return 0;
    }
    if first == 0xE5 {
        lfn_state.active = 0;
        return 1;
    }

    let attr = byte_at(raw, 11);
    if attr == FAT_ATTR_LONG_NAME {
        feed_lfn_entry(raw, lfn_state);
        return 1;
    }
    if (attr & FAT_ATTR_VOLUME_ID) != 0 || first == b'.' {
        lfn_state.active = 0;
        return 1;
    }

    let mut parsed = FatDirEntry {
        name: [0u8; 64],
        name_len: 0,
        attr: attr as u32,
        first_cluster: ((read_le16(raw, 20) as u32) << 16) | (read_le16(raw, 26) as u32),
        file_size: read_le32(raw, 28),
        entry_offset,
    };

    let lfn_len = lfn_length(lfn_state);
    if lfn_len > 0 {
        let mut i = 0usize;
        while i < lfn_len {
            if let Some(slot) = parsed.name.get_mut(i) {
                *slot = byte_at(&lfn_state.buffer, i);
            }
            i += 1;
        }
        parsed.name_len = lfn_len as u32;
    } else {
        let slen = format_short_name(raw, &mut parsed.name);
        parsed.name_len = slen as u32;
    }
    lfn_state.active = 0;

    if parsed.name_len == 0 {
        return 1;
    }
    unsafe {
        *out_entry = parsed;
    }
    2
}

// ====== kfat_entry_matches_name ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_entry_matches_name(
    entry_32: *const u8,
    parsed: *const FatDirEntry,
    target: *const u8,
    target_len: u32,
) -> u32 {
    if entry_32.is_null() || parsed.is_null() || target.is_null() || target_len == 0 || target_len > 63 {
        return 0;
    }
    let p = unsafe { &*parsed };
    let t = unsafe { core::slice::from_raw_parts(target, target_len as usize) };
    if names_equal_ci(&p.name, p.name_len as usize, t, target_len as usize) {
        return 1;
    }
    let raw = unsafe { core::slice::from_raw_parts(entry_32, 32) };
    let mut short_buf = [0u8; 64];
    let short_len = format_short_name(raw, &mut short_buf);
    if names_equal_ci(&short_buf, short_len, t, target_len as usize) {
        1
    } else {
        0
    }
}

// ====== kfat_encode_83_name ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kfat_encode_83_name(
    name: *const u8,
    name_len: u32,
    out_11: *mut u8,
) -> u32 {
    if name.is_null() || out_11.is_null() || name_len == 0 || name_len > 12 {
        return 0;
    }
    let src = unsafe { core::slice::from_raw_parts(name, name_len as usize) };
    let dst = unsafe { core::slice::from_raw_parts_mut(out_11, 11) };

    let mut k = 0usize;
    while k < 11 {
        if let Some(slot) = dst.get_mut(k) {
            *slot = b' ';
        }
        k += 1;
    }

    let mut dot_pos = None;
    let mut i = 0usize;
    while i < name_len as usize {
        let ch = byte_at(src, i);
        if ch == b'.' {
            if dot_pos.is_some() || i == 0 || i > 8 {
                return 0;
            }
            dot_pos = Some(i);
        } else if ch <= 0x20 || matches!(ch, b'/' | b'\\' | b':' | b'*' | b'?' | b'"' | b'<' | b'>' | b'|') {
            return 0;
        }
        i += 1;
    }

    let base_len = match dot_pos {
        Some(pos) => {
            let ext_len = (name_len as usize) - pos - 1;
            if ext_len > 3 {
                return 0;
            }
            let mut e = 0usize;
            while e < ext_len {
                if let Some(slot) = dst.get_mut(8 + e) {
                    *slot = ascii_upper(byte_at(src, pos + 1 + e));
                }
                e += 1;
            }
            pos
        }
        None => {
            if name_len > 8 {
                return 0;
            }
            name_len as usize
        }
    };

    let mut b = 0usize;
    while b < base_len {
        if let Some(slot) = dst.get_mut(b) {
            *slot = ascii_upper(byte_at(src, b));
        }
        b += 1;
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build_fat32_bpb() -> [u8; 512] {
        let mut s = [0u8; 512];
        s[0] = 0xEB;
        s[1] = 0x58;
        s[2] = 0x90;
        s[3..11].copy_from_slice(b"VOIDOS  ");
        s[11..13].copy_from_slice(&512u16.to_le_bytes());
        s[13] = 1; // 1 sector per cluster
        s[14..16].copy_from_slice(&4u16.to_le_bytes()); // 4 reserved sectors
        s[16] = 2; // 2 FATs
        s[17..19].copy_from_slice(&0u16.to_le_bytes()); // RootEntCnt = 0 (FAT32)
        s[32..36].copy_from_slice(&512u32.to_le_bytes()); // 512 total sectors
        s[36..40].copy_from_slice(&4u32.to_le_bytes()); // 4 sectors per FAT
        s[44..48].copy_from_slice(&2u32.to_le_bytes()); // RootClus = 2
        s[510] = 0x55;
        s[511] = 0xAA;
        s
    }

    #[test]
    fn parses_fat32_bpb_and_mbr_partition() {
        let bpb = build_fat32_bpb();
        assert_eq!(detect_partition_slice(&bpb, 512), Ok(0));
        let geom = parse_bpb_slice(&bpb, 0, 512).expect("valid FAT32 BPB");
        assert_eq!(geom.is_fat32, 1);
        assert_eq!(geom.fat_start_lba, 4);
        assert_eq!(geom.data_start_lba, 12);
        assert_eq!(geom.root_cluster, 2);
        assert_eq!(unsafe { kfat_cluster_to_lba(&geom, 2) }, 12);
        assert_eq!(unsafe { kfat_cluster_to_lba(&geom, 3) }, 13);

        // MBR pointing to partition at LBA 63
        let mut mbr = [0u8; 512];
        mbr[446] = 0x80;
        mbr[446 + 4] = 0x06; // FAT16
        mbr[446 + 8..446 + 12].copy_from_slice(&63u32.to_le_bytes());
        mbr[446 + 12..446 + 16].copy_from_slice(&1000u32.to_le_bytes());
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        assert_eq!(detect_partition_slice(&mbr, 2048), Ok(63));
    }

    #[test]
    fn decodes_lfn_and_83_directory_entries() {
        // LFN for "boot-check.txt" (14 chars -> 2 LFN entries: seq 2 with 't', seq 1 with "boot-check.tx")
        let mut lfn2 = [0xFFu8; 32];
        lfn2[0] = 0x42; // last LFN entry, seq 2
        lfn2[11] = FAT_ATTR_LONG_NAME;
        lfn2[1..3].copy_from_slice(&(b't' as u16).to_le_bytes());
        lfn2[3..5].copy_from_slice(&0u16.to_le_bytes());

        let mut lfn1 = [0xFFu8; 32];
        lfn1[0] = 0x01; // seq 1
        lfn1[11] = FAT_ATTR_LONG_NAME;
        let chars = b"boot-check.tx";
        let offsets: [usize; 13] = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
        for (i, &c) in chars.iter().enumerate() {
            lfn1[offsets[i]..offsets[i] + 2].copy_from_slice(&(c as u16).to_le_bytes());
        }

        let mut short = [0u8; 32];
        short[0..11].copy_from_slice(b"BOOT-C~1TXT");
        short[11] = FAT_ATTR_ARCHIVE;
        short[26..28].copy_from_slice(&5u16.to_le_bytes());
        short[28..32].copy_from_slice(&15u32.to_le_bytes());

        let mut state = FatLfnState::default();
        let mut out = FatDirEntry::default();
        assert_eq!(unsafe { kfat_dir_step(lfn2.as_ptr(), 0, &mut state, &mut out) }, 1);
        assert_eq!(unsafe { kfat_dir_step(lfn1.as_ptr(), 32, &mut state, &mut out) }, 1);
        assert_eq!(unsafe { kfat_dir_step(short.as_ptr(), 64, &mut state, &mut out) }, 2);
        assert_eq!(&out.name[..out.name_len as usize], b"boot-check.txt");
        assert_eq!(out.first_cluster, 5);
        assert_eq!(out.file_size, 15);
        assert_eq!(
            unsafe {
                kfat_entry_matches_name(
                    short.as_ptr(),
                    &out,
                    b"BOOT-CHECK.TXT".as_ptr(),
                    14,
                )
            },
            1
        );

        let mut enc = [0u8; 11];
        assert_eq!(unsafe { kfat_encode_83_name(b"hello.txt".as_ptr(), 9, enc.as_mut_ptr()) }, 1);
        assert_eq!(&enc, b"HELLO   TXT");
    }
}
