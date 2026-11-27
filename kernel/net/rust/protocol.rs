#![cfg_attr(not(test), no_std)]

#[cfg(not(test))]
use core::panic::PanicInfo;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

pub const ETHERTYPE_IPV4: u16 = 0x0800;
pub const ETHERTYPE_ARP: u16 = 0x0806;

pub const IP_PROTO_ICMP: u8 = 1;
pub const IP_PROTO_TCP: u8 = 6;
pub const IP_PROTO_UDP: u8 = 17;

pub const ICMP_ECHO_REPLY: u8 = 0;
pub const ICMP_ECHO_REQUEST: u8 = 8;

pub const DHCP_CLIENT_PORT: u16 = 68;
pub const DHCP_SERVER_PORT: u16 = 67;
pub const DHCP_MAGIC_COOKIE: u32 = 0x6382_5363;

pub const DHCP_MSG_DISCOVER: u8 = 1;
pub const DHCP_MSG_OFFER: u8 = 2;
pub const DHCP_MSG_REQUEST: u8 = 3;
pub const DHCP_MSG_ACK: u8 = 5;
pub const DHCP_MSG_NAK: u8 = 6;

// ====== FFI structs ======

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetEthInfo {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub ethertype: u16,
    pub payload_offset: u16,
    pub payload_len: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetArpInfo {
    pub opcode: u16,
    pub sender_mac: [u8; 6],
    pub target_mac: [u8; 6],
    pub sender_ip: u32,
    pub target_ip: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetIpv4Info {
    pub src_ip: u32,
    pub dst_ip: u32,
    pub protocol: u8,
    pub ttl: u8,
    pub header_len: u16,
    pub payload_len: u16,
    pub identification: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetIcmpInfo {
    pub icmp_type: u8,
    pub code: u8,
    pub id: u16,
    pub seq: u16,
    pub payload_len: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetUdpInfo {
    pub src_port: u16,
    pub dst_port: u16,
    pub payload_len: u16,
    pub reserved: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KnetDhcpReply {
    pub xid: u32,
    pub your_ip: u32,
    pub server_ip: u32,
    pub subnet_mask: u32,
    pub gateway_ip: u32,
    pub dns_ip: u32,
    pub lease_seconds: u32,
    pub msg_type: u8,
    pub reserved: [u8; 3],
}

// ====== helpers (panic-free for no_std kernel .obj linking) ======

#[inline(always)]
fn read_u8(buf: &[u8], off: usize) -> u8 {
    buf.get(off).copied().unwrap_or(0)
}

#[inline(always)]
fn write_u8(buf: &mut [u8], off: usize, val: u8) {
    if let Some(slot) = buf.get_mut(off) {
        *slot = val;
    }
}

#[inline(always)]
fn read_be16(buf: &[u8], off: usize) -> u16 {
    u16::from_be_bytes([read_u8(buf, off), read_u8(buf, off + 1)])
}

#[inline(always)]
fn read_be32(buf: &[u8], off: usize) -> u32 {
    u32::from_be_bytes([
        read_u8(buf, off),
        read_u8(buf, off + 1),
        read_u8(buf, off + 2),
        read_u8(buf, off + 3),
    ])
}

#[inline(always)]
fn write_be16(buf: &mut [u8], off: usize, val: u16) {
    let b = val.to_be_bytes();
    write_u8(buf, off, b[0]);
    write_u8(buf, off + 1, b[1]);
}

#[inline(always)]
fn write_be32(buf: &mut [u8], off: usize, val: u32) {
    let b = val.to_be_bytes();
    write_u8(buf, off, b[0]);
    write_u8(buf, off + 1, b[1]);
    write_u8(buf, off + 2, b[2]);
    write_u8(buf, off + 3, b[3]);
}

#[inline(always)]
fn copy_bytes_safe(dst: &mut [u8], dst_off: usize, src: &[u8], src_off: usize, len: usize) {
    for i in 0..len {
        write_u8(dst, dst_off + i, read_u8(src, src_off + i));
    }
}

// RFC 1071 Internet Checksum
pub fn internet_checksum(data: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut i = 0;
    while i + 1 < data.len() {
        sum += u32::from(u16::from_be_bytes([read_u8(data, i), read_u8(data, i + 1)]));
        i += 2;
    }
    if i < data.len() {
        sum += u32::from(read_u8(data, i)) << 8;
    }
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    !(sum as u16)
}

pub fn transport_checksum(src_ip: u32, dst_ip: u32, proto: u8, segment: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += u32::from(proto);
    sum += (segment.len() as u32) & 0xFFFF;
    let mut i = 0;
    while i + 1 < segment.len() {
        sum += u32::from(u16::from_be_bytes([read_u8(segment, i), read_u8(segment, i + 1)]));
        i += 2;
    }
    if i < segment.len() {
        sum += u32::from(read_u8(segment, i)) << 8;
    }
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    let res = !(sum as u16);
    if proto == IP_PROTO_UDP && res == 0 {
        0xFFFF
    } else {
        res
    }
}

// ====== Ethernet II (RFC 894) ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_eth_parse(
    frame: *const u8,
    frame_len: u32,
    out: *mut KnetEthInfo,
) -> i32 {
    if frame.is_null() || out.is_null() || frame_len < 14 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(frame, frame_len as usize) };
    let mut info = KnetEthInfo::default();
    copy_bytes_safe(&mut info.dst, 0, buf, 0, 6);
    copy_bytes_safe(&mut info.src, 0, buf, 6, 6);
    info.ethertype = read_be16(buf, 12);
    info.payload_offset = 14;
    info.payload_len = frame_len - 14;
    unsafe { *out = info };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_eth_build(
    out: *mut u8,
    cap: u32,
    dst: *const u8,
    src: *const u8,
    ethertype: u16,
    payload: *const u8,
    payload_len: u32,
) -> u32 {
    if out.is_null() || dst.is_null() || src.is_null() || (payload_len > 0 && payload.is_null()) {
        return 0;
    }
    let total = 14u32.saturating_add(payload_len);
    if total > cap || total > 1518 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, total as usize) };
    let dst_s = unsafe { core::slice::from_raw_parts(dst, 6) };
    let src_s = unsafe { core::slice::from_raw_parts(src, 6) };
    copy_bytes_safe(buf, 0, dst_s, 0, 6);
    copy_bytes_safe(buf, 6, src_s, 0, 6);
    write_be16(buf, 12, ethertype);
    if payload_len > 0 {
        let pay = unsafe { core::slice::from_raw_parts(payload, payload_len as usize) };
        copy_bytes_safe(buf, 14, pay, 0, payload_len as usize);
    }
    total
}

// ====== ARP (RFC 826) ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_arp_parse(
    payload: *const u8,
    len: u32,
    out: *mut KnetArpInfo,
) -> i32 {
    if payload.is_null() || out.is_null() || len < 28 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(payload, len as usize) };
    if read_be16(buf, 0) != 1 || read_be16(buf, 2) != ETHERTYPE_IPV4 || read_u8(buf, 4) != 6 || read_u8(buf, 5) != 4 {
        return 0;
    }
    let mut info = KnetArpInfo::default();
    info.opcode = read_be16(buf, 6);
    copy_bytes_safe(&mut info.sender_mac, 0, buf, 8, 6);
    info.sender_ip = read_be32(buf, 14);
    copy_bytes_safe(&mut info.target_mac, 0, buf, 18, 6);
    info.target_ip = read_be32(buf, 24);
    unsafe { *out = info };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_arp_build(
    out: *mut u8,
    cap: u32,
    opcode: u16,
    sender_mac: *const u8,
    sender_ip: u32,
    target_mac: *const u8,
    target_ip: u32,
) -> u32 {
    if out.is_null() || sender_mac.is_null() || target_mac.is_null() || cap < 28 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, 28) };
    write_be16(buf, 0, 1);
    write_be16(buf, 2, ETHERTYPE_IPV4);
    write_u8(buf, 4, 6);
    write_u8(buf, 5, 4);
    write_be16(buf, 6, opcode);
    let smac = unsafe { core::slice::from_raw_parts(sender_mac, 6) };
    let tmac = unsafe { core::slice::from_raw_parts(target_mac, 6) };
    copy_bytes_safe(buf, 8, smac, 0, 6);
    write_be32(buf, 14, sender_ip);
    copy_bytes_safe(buf, 18, tmac, 0, 6);
    write_be32(buf, 24, target_ip);
    28
}

// ====== IPv4 (RFC 791) ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_ipv4_parse(
    packet: *const u8,
    len: u32,
    out: *mut KnetIpv4Info,
) -> i32 {
    if packet.is_null() || out.is_null() || len < 20 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(packet, len as usize) };
    let v_ihl = read_u8(buf, 0);
    let ver = v_ihl >> 4;
    let ihl = ((v_ihl & 0x0F) as usize) * 4;
    if ver != 4 || ihl < 20 || ihl > buf.len() {
        return 0;
    }
    let total_len = read_be16(buf, 2) as usize;
    if total_len < ihl || total_len > buf.len() {
        return 0;
    }
    let hdr = match buf.get(..ihl) {
        Some(h) => h,
        None => return 0,
    };
    if internet_checksum(hdr) != 0 {
        return 0;
    }
    let info = KnetIpv4Info {
        src_ip: read_be32(buf, 12),
        dst_ip: read_be32(buf, 16),
        protocol: read_u8(buf, 9),
        ttl: read_u8(buf, 8),
        header_len: ihl as u16,
        payload_len: (total_len - ihl) as u16,
        identification: read_be16(buf, 4),
    };
    unsafe { *out = info };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_ipv4_build(
    out: *mut u8,
    cap: u32,
    src_ip: u32,
    dst_ip: u32,
    proto: u8,
    ident: u16,
    payload: *const u8,
    payload_len: u32,
) -> u32 {
    if out.is_null() || (payload_len > 0 && payload.is_null()) {
        return 0;
    }
    let total = 20u32.saturating_add(payload_len);
    if total > cap || total > 1500 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, total as usize) };
    write_u8(buf, 0, 0x45);
    write_u8(buf, 1, 0);
    write_be16(buf, 2, total as u16);
    write_be16(buf, 4, ident);
    write_be16(buf, 6, 0x4000);
    write_u8(buf, 8, 64);
    write_u8(buf, 9, proto);
    write_be16(buf, 10, 0);
    write_be32(buf, 12, src_ip);
    write_be32(buf, 16, dst_ip);
    let csum = match buf.get(..20) {
        Some(h) => internet_checksum(h),
        None => return 0,
    };
    write_be16(buf, 10, csum);
    if payload_len > 0 {
        let pay = unsafe { core::slice::from_raw_parts(payload, payload_len as usize) };
        copy_bytes_safe(buf, 20, pay, 0, payload_len as usize);
    }
    total
}

// ====== ICMP Echo (RFC 792) ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_icmp_parse(
    payload: *const u8,
    len: u32,
    out: *mut KnetIcmpInfo,
) -> i32 {
    if payload.is_null() || out.is_null() || len < 8 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(payload, len as usize) };
    if internet_checksum(buf) != 0 {
        return 0;
    }
    let info = KnetIcmpInfo {
        icmp_type: read_u8(buf, 0),
        code: read_u8(buf, 1),
        id: read_be16(buf, 4),
        seq: read_be16(buf, 6),
        payload_len: (len - 8) as u16,
    };
    unsafe { *out = info };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_icmp_build_echo(
    out: *mut u8,
    cap: u32,
    icmp_type: u8,
    id: u16,
    seq: u16,
    data: *const u8,
    data_len: u32,
) -> u32 {
    if out.is_null() || (data_len > 0 && data.is_null()) {
        return 0;
    }
    let total = 8u32.saturating_add(data_len);
    if total > cap || total > 1480 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, total as usize) };
    write_u8(buf, 0, icmp_type);
    write_u8(buf, 1, 0);
    write_be16(buf, 2, 0);
    write_be16(buf, 4, id);
    write_be16(buf, 6, seq);
    if data_len > 0 {
        let src = unsafe { core::slice::from_raw_parts(data, data_len as usize) };
        copy_bytes_safe(buf, 8, src, 0, data_len as usize);
    }
    let csum = internet_checksum(buf);
    write_be16(buf, 2, csum);
    total
}

// ====== UDP (RFC 768) ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_udp_parse(
    payload: *const u8,
    len: u32,
    out: *mut KnetUdpInfo,
) -> i32 {
    if payload.is_null() || out.is_null() || len < 8 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(payload, len as usize) };
    let udp_len = read_be16(buf, 4) as u32;
    if udp_len < 8 || udp_len > len {
        return 0;
    }
    let info = KnetUdpInfo {
        src_port: read_be16(buf, 0),
        dst_port: read_be16(buf, 2),
        payload_len: (udp_len - 8) as u16,
        reserved: 0,
    };
    unsafe { *out = info };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_udp_build(
    out: *mut u8,
    cap: u32,
    src_ip: u32,
    dst_ip: u32,
    src_port: u16,
    dst_port: u16,
    data: *const u8,
    data_len: u32,
) -> u32 {
    if out.is_null() || (data_len > 0 && data.is_null()) {
        return 0;
    }
    let total = 8u32.saturating_add(data_len);
    if total > cap || total > 1480 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, total as usize) };
    write_be16(buf, 0, src_port);
    write_be16(buf, 2, dst_port);
    write_be16(buf, 4, total as u16);
    write_be16(buf, 6, 0);
    if data_len > 0 {
        let src = unsafe { core::slice::from_raw_parts(data, data_len as usize) };
        copy_bytes_safe(buf, 8, src, 0, data_len as usize);
    }
    let csum = transport_checksum(src_ip, dst_ip, IP_PROTO_UDP, buf);
    write_be16(buf, 6, csum);
    total
}

// ====== DHCPv4 (RFC 2131) ======

fn build_dhcp_common(buf: &mut [u8], mac: &[u8], xid: u32) {
    for b in buf.iter_mut() {
        *b = 0;
    }
    write_u8(buf, 0, 1);
    write_u8(buf, 1, 1);
    write_u8(buf, 2, 6);
    write_be32(buf, 4, xid);
    write_be16(buf, 10, 0x8000);
    copy_bytes_safe(buf, 28, mac, 0, 6);
    write_be32(buf, 236, DHCP_MAGIC_COOKIE);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_dhcp_build_discover(
    out: *mut u8,
    cap: u32,
    mac: *const u8,
    xid: u32,
) -> u32 {
    if out.is_null() || mac.is_null() || cap < 300 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, 300) };
    let mac_s = unsafe { core::slice::from_raw_parts(mac, 6) };
    build_dhcp_common(buf, mac_s, xid);
    copy_bytes_safe(buf, 240, &[53, 1, DHCP_MSG_DISCOVER], 0, 3);
    copy_bytes_safe(buf, 243, &[55, 3, 1, 3, 6], 0, 5);
    write_u8(buf, 248, 255);
    300
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_dhcp_build_request(
    out: *mut u8,
    cap: u32,
    mac: *const u8,
    xid: u32,
    requested_ip: u32,
    server_ip: u32,
) -> u32 {
    if out.is_null() || mac.is_null() || cap < 300 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts_mut(out, 300) };
    let mac_s = unsafe { core::slice::from_raw_parts(mac, 6) };
    build_dhcp_common(buf, mac_s, xid);
    copy_bytes_safe(buf, 240, &[53, 1, DHCP_MSG_REQUEST], 0, 3);
    write_u8(buf, 243, 50);
    write_u8(buf, 244, 4);
    write_be32(buf, 245, requested_ip);
    write_u8(buf, 249, 54);
    write_u8(buf, 250, 4);
    write_be32(buf, 251, server_ip);
    copy_bytes_safe(buf, 255, &[55, 3, 1, 3, 6], 0, 5);
    write_u8(buf, 260, 255);
    300
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knet_dhcp_parse_reply(
    payload: *const u8,
    len: u32,
    out: *mut KnetDhcpReply,
) -> i32 {
    if payload.is_null() || out.is_null() || len < 244 {
        return 0;
    }
    let buf = unsafe { core::slice::from_raw_parts(payload, len as usize) };
    if read_u8(buf, 0) != 2 || read_be32(buf, 236) != DHCP_MAGIC_COOKIE {
        return 0;
    }
    let mut reply = KnetDhcpReply {
        xid: read_be32(buf, 4),
        your_ip: read_be32(buf, 16),
        server_ip: read_be32(buf, 20),
        subnet_mask: 0xFFFF_FF00,
        gateway_ip: 0,
        dns_ip: 0,
        lease_seconds: 3600,
        msg_type: 0,
        reserved: [0; 3],
    };
    let mut i = 240usize;
    while i < buf.len() {
        let opt = read_u8(buf, i);
        if opt == 0 {
            i += 1;
            continue;
        }
        if opt == 255 {
            break;
        }
        if i + 1 >= buf.len() {
            break;
        }
        let olen = read_u8(buf, i + 1) as usize;
        if i + 2 + olen > buf.len() {
            break;
        }
        let val_off = i + 2;
        match opt {
            53 if olen == 1 => reply.msg_type = read_u8(buf, val_off),
            1 if olen >= 4 => reply.subnet_mask = read_be32(buf, val_off),
            3 if olen >= 4 => reply.gateway_ip = read_be32(buf, val_off),
            6 if olen >= 4 => reply.dns_ip = read_be32(buf, val_off),
            51 if olen >= 4 => reply.lease_seconds = read_be32(buf, val_off),
            54 if olen >= 4 => reply.server_ip = read_be32(buf, val_off),
            _ => {}
        }
        i += 2 + olen;
    }
    if reply.msg_type == 0 {
        return 0;
    }
    unsafe { *out = reply };
    1
}

// ====== host unit tests ======

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn eth_arp_roundtrip_and_reject() {
        let smac = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];
        let bcast = [0xFF; 6];
        let mut arp = [0u8; 28];
        let n = unsafe {
            knet_arp_build(arp.as_mut_ptr(), 28, 1, smac.as_ptr(), 0x0A00_020F, [0; 6].as_ptr(), 0x0A00_0202)
        };
        assert_eq!(n, 28);

        let mut frame = [0u8; 64];
        let flen = unsafe {
            knet_eth_build(frame.as_mut_ptr(), 64, bcast.as_ptr(), smac.as_ptr(), ETHERTYPE_ARP, arp.as_ptr(), n)
        };
        assert_eq!(flen, 42);

        let mut einfo = KnetEthInfo::default();
        assert_eq!(unsafe { knet_eth_parse(frame.as_ptr(), flen, &mut einfo) }, 1);
        assert_eq!(einfo.ethertype, ETHERTYPE_ARP);
        assert_eq!(einfo.payload_len, 28);

        let mut ainfo = KnetArpInfo::default();
        assert_eq!(unsafe { knet_arp_parse(frame[14..].as_ptr(), 28, &mut ainfo) }, 1);
        assert_eq!(ainfo.opcode, 1);
        assert_eq!(ainfo.sender_ip, 0x0A00_020F);
        assert_eq!(ainfo.target_ip, 0x0A00_0202);

        // Truncated ARP must be rejected.
        assert_eq!(unsafe { knet_arp_parse(frame[14..].as_ptr(), 20, &mut ainfo) }, 0);
    }

    #[test]
    fn ipv4_icmp_udp_roundtrip_and_checksum() {
        let mut icmp = [0u8; 64];
        let data = b"VoidOS!";
        let ilen = unsafe {
            knet_icmp_build_echo(icmp.as_mut_ptr(), 64, ICMP_ECHO_REQUEST, 0x1234, 7, data.as_ptr(), data.len() as u32)
        };
        assert_eq!(ilen, 15);

        let mut ip = [0u8; 128];
        let plen = unsafe {
            knet_ipv4_build(ip.as_mut_ptr(), 128, 0x0A00_020F, 0x0A00_0202, IP_PROTO_ICMP, 1, icmp.as_ptr(), ilen)
        };
        assert_eq!(plen, 35);

        let mut ip_info = KnetIpv4Info::default();
        assert_eq!(unsafe { knet_ipv4_parse(ip.as_ptr(), plen, &mut ip_info) }, 1);
        assert_eq!(ip_info.protocol, IP_PROTO_ICMP);
        assert_eq!(ip_info.payload_len, 15);

        let mut icmp_info = KnetIcmpInfo::default();
        assert_eq!(unsafe { knet_icmp_parse(ip[20..].as_ptr(), 15, &mut icmp_info) }, 1);
        assert_eq!(icmp_info.id, 0x1234);
        assert_eq!(icmp_info.seq, 7);

        // Corrupt IPv4 header checksum -> must reject.
        ip[12] ^= 0x01;
        assert_eq!(unsafe { knet_ipv4_parse(ip.as_ptr(), plen, &mut ip_info) }, 0);

        // UDP build + parse.
        let mut udp = [0u8; 64];
        let ulen = unsafe {
            knet_udp_build(udp.as_mut_ptr(), 64, 0x0A00_020F, 0x0A00_0203, 12345, 53, data.as_ptr(), data.len() as u32)
        };
        assert_eq!(ulen, 15);
        let mut uinfo = KnetUdpInfo::default();
        assert_eq!(unsafe { knet_udp_parse(udp.as_ptr(), ulen, &mut uinfo) }, 1);
        assert_eq!(uinfo.src_port, 12345);
        assert_eq!(uinfo.dst_port, 53);
        assert_eq!(uinfo.payload_len, 7);
    }

    #[test]
    fn dhcp_build_and_reply_parse() {
        let mac = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];
        let mut pkt = [0u8; 300];
        assert_eq!(unsafe { knet_dhcp_build_discover(pkt.as_mut_ptr(), 300, mac.as_ptr(), 0xDEAD_BEEF) }, 300);
        assert_eq!(read_be32(&pkt, 236), DHCP_MAGIC_COOKIE);

        // Forge a valid DHCP OFFER reply.
        let mut offer = [0u8; 300];
        offer[0] = 2; // BOOTREPLY
        write_be32(&mut offer, 4, 0xDEAD_BEEF);
        write_be32(&mut offer, 16, 0x0A00_020F); // 10.0.2.15
        write_be32(&mut offer, 236, DHCP_MAGIC_COOKIE);
        let opts: &[u8] = &[
            53, 1, DHCP_MSG_OFFER,
            1, 4, 255, 255, 255, 0,
            3, 4, 10, 0, 2, 2,
            6, 4, 10, 0, 2, 3,
            54, 4, 10, 0, 2, 2,
            255,
        ];
        offer[240..240 + opts.len()].copy_from_slice(opts);

        let mut parsed = KnetDhcpReply::default();
        assert_eq!(unsafe { knet_dhcp_parse_reply(offer.as_ptr(), 300, &mut parsed) }, 1);
        assert_eq!(parsed.msg_type, DHCP_MSG_OFFER);
        assert_eq!(parsed.your_ip, 0x0A00_020F);
        assert_eq!(parsed.gateway_ip, 0x0A00_0202);
        assert_eq!(parsed.dns_ip, 0x0A00_0203);
        assert_eq!(parsed.server_ip, 0x0A00_0202);

        // Bad magic cookie must reject.
        offer[236] ^= 0xFF;
        assert_eq!(unsafe { knet_dhcp_parse_reply(offer.as_ptr(), 300, &mut parsed) }, 0);
    }
}
