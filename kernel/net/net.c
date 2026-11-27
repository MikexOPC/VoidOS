#include "net.h"
#include "kernel/diagnostics/log.h"

#define KNET_ARP_CACHE_CAP 64U

typedef struct {
    uint32_t  Ip;
    uint8_t   Mac[6];
    uint16_t  Valid;
} KNET_ARP_ENTRY;

typedef struct {
    KERNEL_NET_DEVICE  *Dev;
    KERNEL_CLOCK        Clock;
    uint32_t            HasClock;
    uint32_t            Configured;
    uint32_t            Ip;
    uint32_t            Mask;
    uint32_t            Gateway;
    uint32_t            Dns;
    uint32_t            DhcpServer;
    uint16_t            NextIdent;
    uint16_t            NextPingSeq;
    KNET_ARP_ENTRY      Arp[KNET_ARP_CACHE_CAP];
    uint32_t            ArpCursor;
    // Active ICMP Echo Reply waiter
    uint32_t            PingWaitActive;
    uint16_t            PingWaitId;
    uint16_t            PingWaitSeq;
    uint32_t            PingReplyReceived;
    // Active UDP response waiter
    uint32_t            UdpWaitActive;
    uint16_t            UdpWaitPort;
    uint8_t            *UdpWaitBuf;
    uint32_t            UdpWaitCap;
    uint32_t            UdpWaitLen;
    uint32_t            UdpWaitReceived;
} KERNEL_NET_STATE;

static KERNEL_NET_STATE knet_state;
static const uint8_t KNET_BCAST_MAC[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
static const uint8_t KNET_ZERO_MAC[6] = {0U, 0U, 0U, 0U, 0U, 0U};

// ====== clock_ms_private ======

static uint32_t ticks_for_ms_private(uint32_t ms)
{
    uint64_t ticks = ((uint64_t)ms * (uint64_t)knet_state.Clock.Hz) / 1000ULL;
    return ticks ? (uint32_t)ticks : 1U;
}

static uint32_t elapsed_ms_private(uint32_t start_ticks)
{
    if (!knet_state.HasClock)
        return 0U;
    uint32_t dt = kernel_clock_elapsed(&knet_state.Clock, start_ticks);
    return (uint32_t)(((uint64_t)dt * 1000ULL) / (uint64_t)knet_state.Clock.Hz);
}

// ====== arp_store_private ======

static void arp_store_private(uint32_t ip, const uint8_t mac[6])
{
    if (!ip)
        return;
    for (uint32_t i = 0; i < KNET_ARP_CACHE_CAP; ++i) {
        if (knet_state.Arp[i].Valid && knet_state.Arp[i].Ip == ip) {
            for (uint32_t j = 0; j < 6U; ++j)
                knet_state.Arp[i].Mac[j] = mac[j];
            return;
        }
    }
    uint32_t slot = knet_state.ArpCursor % KNET_ARP_CACHE_CAP;
    knet_state.ArpCursor = (slot + 1U) % KNET_ARP_CACHE_CAP;
    knet_state.Arp[slot].Ip = ip;
    for (uint32_t j = 0; j < 6U; ++j)
        knet_state.Arp[slot].Mac[j] = mac[j];
    knet_state.Arp[slot].Valid = 1U;
}

// ====== arp_lookup_private ======

static int arp_lookup_private(uint32_t ip, uint8_t mac_out[6])
{
    for (uint32_t i = 0; i < KNET_ARP_CACHE_CAP; ++i) {
        if (knet_state.Arp[i].Valid && knet_state.Arp[i].Ip == ip) {
            for (uint32_t j = 0; j < 6U; ++j)
                mac_out[j] = knet_state.Arp[i].Mac[j];
            return 1;
        }
    }
    return 0;
}

// ====== send_eth_private ======

static int send_eth_private(const uint8_t dst_mac[6], uint16_t ethertype, const uint8_t *payload, uint32_t len)
{
    if (!knet_state.Dev || !knet_state.Dev->Ops || !knet_state.Dev->Ops->SendFrame)
        return KERNEL_NET_ERR_IO;
    uint8_t frame[KERNEL_NET_MAX_FRAME_BYTES];
    uint32_t flen = knet_eth_build(frame, sizeof(frame), dst_mac, knet_state.Dev->Mac, ethertype, payload, len);
    if (!flen)
        return KERNEL_NET_ERR_PARAM;
    return knet_state.Dev->Ops->SendFrame(knet_state.Dev->Context, frame, flen);
}

// ====== send_ipv4_mac_private ======

static int send_ipv4_mac_private(
    const uint8_t dst_mac[6],
    uint32_t src_ip,
    uint32_t dst_ip,
    uint8_t proto,
    const uint8_t *payload,
    uint32_t payload_len
)
{
    uint8_t ip_pkt[KERNEL_NET_MTU];
    uint16_t id = ++knet_state.NextIdent;
    uint32_t ilen = knet_ipv4_build(ip_pkt, sizeof(ip_pkt), src_ip, dst_ip, proto, id, payload, payload_len);
    if (!ilen)
        return KERNEL_NET_ERR_PARAM;
    return send_eth_private(dst_mac, KNET_ETHERTYPE_IPV4, ip_pkt, ilen);
}

// ====== handle_frame_private ======

static void handle_frame_private(const uint8_t *frame, uint32_t len)
{
    KNET_ETH_INFO eth;
    if (!knet_eth_parse(frame, len, &eth))
        return;

    const uint8_t *l3 = frame + eth.PayloadOffset;
    uint32_t l3_len = eth.PayloadLen;

    if (eth.EtherType == KNET_ETHERTYPE_ARP) {
        KNET_ARP_INFO arp;
        if (!knet_arp_parse(l3, l3_len, &arp))
            return;
        arp_store_private(arp.SenderIp, arp.SenderMac);
        if (arp.Opcode == 1U && knet_state.Configured && arp.TargetIp == knet_state.Ip) {
            uint8_t reply[28];
            uint32_t rlen = knet_arp_build(
                reply, sizeof(reply), 2U,
                knet_state.Dev->Mac, knet_state.Ip,
                arp.SenderMac, arp.SenderIp
            );
            if (rlen)
                (void)send_eth_private(arp.SenderMac, KNET_ETHERTYPE_ARP, reply, rlen);
        }
        return;
    }

    if (eth.EtherType != KNET_ETHERTYPE_IPV4)
        return;

    KNET_IPV4_INFO ip;
    if (!knet_ipv4_parse(l3, l3_len, &ip))
        return;
    arp_store_private(ip.SrcIp, eth.Src);

    const uint8_t *l4 = l3 + ip.HeaderLen;
    uint32_t l4_len = ip.PayloadLen;

    if (ip.Protocol == KNET_IP_PROTO_ICMP) {
        KNET_ICMP_INFO icmp;
        if (!knet_icmp_parse(l4, l4_len, &icmp))
            return;
        if (icmp.IcmpType == KNET_ICMP_ECHO_REQUEST && knet_state.Configured && ip.DstIp == knet_state.Ip) {
            uint8_t reply[1480];
            uint32_t rlen = knet_icmp_build_echo(
                reply, sizeof(reply), KNET_ICMP_ECHO_REPLY,
                icmp.Id, icmp.Seq, l4 + 8U, icmp.PayloadLen
            );
            if (rlen)
                (void)send_ipv4_mac_private(eth.Src, knet_state.Ip, ip.SrcIp, KNET_IP_PROTO_ICMP, reply, rlen);
        } else if (icmp.IcmpType == KNET_ICMP_ECHO_REPLY && knet_state.PingWaitActive &&
                   icmp.Id == knet_state.PingWaitId && icmp.Seq == knet_state.PingWaitSeq) {
            knet_state.PingReplyReceived = 1U;
        }
        return;
    }

    if (ip.Protocol == KNET_IP_PROTO_UDP) {
        KNET_UDP_INFO udp;
        if (!knet_udp_parse(l4, l4_len, &udp))
            return;
        if (knet_state.UdpWaitActive && udp.DstPort == knet_state.UdpWaitPort &&
            !knet_state.UdpWaitReceived && knet_state.UdpWaitBuf) {
            uint32_t copy_len = udp.PayloadLen;
            if (copy_len > knet_state.UdpWaitCap)
                copy_len = knet_state.UdpWaitCap;
            for (uint32_t i = 0; i < copy_len; ++i)
                knet_state.UdpWaitBuf[i] = l4[8U + i];
            knet_state.UdpWaitLen = copy_len;
            knet_state.UdpWaitReceived = 1U;
        }
    }
}

// ====== kernel_net_poll ======

void kernel_net_poll(void)
{
    if (!knet_state.Dev || !knet_state.Dev->Ops || !knet_state.Dev->Ops->PollRx)
        return;
    uint8_t frame[KERNEL_NET_MAX_FRAME_BYTES];
    for (uint32_t burst = 0; burst < 32U; ++burst) {
        uint32_t len = 0U;
        int rc = knet_state.Dev->Ops->PollRx(knet_state.Dev->Context, frame, sizeof(frame), &len);
        if (rc != KERNEL_NET_OK || len < KERNEL_NET_ETH_HEADER_BYTES)
            break;
        handle_frame_private(frame, len);
    }
}

// ====== dhcp_exchange_private ======

static int dhcp_exchange_private(void)
{
    uint32_t xid = 0x564F4944U ^ ((uint32_t)knet_state.Dev->Mac[4] << 8) ^ knet_state.Dev->Mac[5];
    uint8_t dhcp_tx[320];
    uint8_t udp_tx[360];
    uint8_t dhcp_rx[576];

    // Step 1: DHCP DISCOVER -> OFFER
    uint32_t dlen = knet_dhcp_build_discover(dhcp_tx, sizeof(dhcp_tx), knet_state.Dev->Mac, xid);
    if (!dlen)
        return 0;
    uint32_t ulen = knet_udp_build(
        udp_tx, sizeof(udp_tx), 0U, 0xFFFFFFFFU,
        KNET_DHCP_CLIENT_PORT, KNET_DHCP_SERVER_PORT, dhcp_tx, dlen
    );
    if (!ulen)
        return 0;

    KNET_DHCP_REPLY offer = {0};
    int got_offer = 0;
    for (uint32_t attempt = 0; attempt < 3U && !got_offer; ++attempt) {
        knet_state.UdpWaitActive = 1U;
        knet_state.UdpWaitPort = KNET_DHCP_CLIENT_PORT;
        knet_state.UdpWaitBuf = dhcp_rx;
        knet_state.UdpWaitCap = sizeof(dhcp_rx);
        knet_state.UdpWaitLen = 0U;
        knet_state.UdpWaitReceived = 0U;

        (void)send_ipv4_mac_private(KNET_BCAST_MAC, 0U, 0xFFFFFFFFU, KNET_IP_PROTO_UDP, udp_tx, ulen);

        uint32_t start = 0U;
        if (knet_state.HasClock)
            (void)kernel_clock_now(&knet_state.Clock, &start);
        uint32_t limit = ticks_for_ms_private(1000U);
        for (uint32_t spin = 0; spin < 200000U; ++spin) {
            kernel_net_poll();
            if (knet_state.UdpWaitReceived) {
                KNET_DHCP_REPLY rep;
                if (knet_dhcp_parse_reply(dhcp_rx, knet_state.UdpWaitLen, &rep) &&
                    rep.Xid == xid && rep.MsgType == KNET_DHCP_OFFER && rep.YourIp != 0U) {
                    offer = rep;
                    got_offer = 1;
                    break;
                }
                knet_state.UdpWaitReceived = 0U;
            }
            if (knet_state.HasClock && kernel_clock_elapsed(&knet_state.Clock, start) >= limit)
                break;
        }
        knet_state.UdpWaitActive = 0U;
    }
    if (!got_offer)
        return 0;

    // Step 2: DHCP REQUEST -> ACK
    uint32_t server_ip = offer.ServerIp ? offer.ServerIp : offer.GatewayIp;
    dlen = knet_dhcp_build_request(dhcp_tx, sizeof(dhcp_tx), knet_state.Dev->Mac, xid, offer.YourIp, server_ip);
    if (!dlen)
        return 0;
    ulen = knet_udp_build(
        udp_tx, sizeof(udp_tx), 0U, 0xFFFFFFFFU,
        KNET_DHCP_CLIENT_PORT, KNET_DHCP_SERVER_PORT, dhcp_tx, dlen
    );
    if (!ulen)
        return 0;

    KNET_DHCP_REPLY ack = {0};
    int got_ack = 0;
    for (uint32_t attempt = 0; attempt < 3U && !got_ack; ++attempt) {
        knet_state.UdpWaitActive = 1U;
        knet_state.UdpWaitPort = KNET_DHCP_CLIENT_PORT;
        knet_state.UdpWaitBuf = dhcp_rx;
        knet_state.UdpWaitCap = sizeof(dhcp_rx);
        knet_state.UdpWaitLen = 0U;
        knet_state.UdpWaitReceived = 0U;

        (void)send_ipv4_mac_private(KNET_BCAST_MAC, 0U, 0xFFFFFFFFU, KNET_IP_PROTO_UDP, udp_tx, ulen);

        uint32_t start = 0U;
        if (knet_state.HasClock)
            (void)kernel_clock_now(&knet_state.Clock, &start);
        uint32_t limit = ticks_for_ms_private(1000U);
        for (uint32_t spin = 0; spin < 200000U; ++spin) {
            kernel_net_poll();
            if (knet_state.UdpWaitReceived) {
                KNET_DHCP_REPLY rep;
                if (knet_dhcp_parse_reply(dhcp_rx, knet_state.UdpWaitLen, &rep) &&
                    rep.Xid == xid && rep.MsgType == KNET_DHCP_ACK && rep.YourIp != 0U) {
                    ack = rep;
                    got_ack = 1;
                    break;
                }
                knet_state.UdpWaitReceived = 0U;
            }
            if (knet_state.HasClock && kernel_clock_elapsed(&knet_state.Clock, start) >= limit)
                break;
        }
        knet_state.UdpWaitActive = 0U;
    }
    if (!got_ack)
        return 0;

    knet_state.Ip = ack.YourIp;
    knet_state.Mask = ack.SubnetMask ? ack.SubnetMask : 0xFFFFFF00U;
    knet_state.Gateway = ack.GatewayIp ? ack.GatewayIp : offer.GatewayIp;
    knet_state.Dns = ack.DnsIp ? ack.DnsIp : offer.DnsIp;
    knet_state.DhcpServer = server_ip;
    knet_state.Configured = 1U;
    return 1;
}

// ====== kernel_net_start ======

int kernel_net_start(KERNEL_NET_DEVICE *dev, const KERNEL_CLOCK *clock)
{
    for (uint32_t i = 0; i < sizeof(knet_state); ++i)
        ((uint8_t *)&knet_state)[i] = 0U;
    if (!dev)
        return 1;

    knet_state.Dev = dev;
    if (clock && clock->Read && clock->Hz) {
        knet_state.Clock = *clock;
        knet_state.HasClock = 1U;
    }

    if (dhcp_exchange_private()) {
        (void)(kernel_text("KERNEL_NET_IP: ") && kernel_number(knet_state.Ip) &&
            kernel_text("\nKERNEL_NET_GW: ") && kernel_number(knet_state.Gateway) &&
            kernel_text("\nKERNEL_NET_DNS: ") && kernel_number(knet_state.Dns) &&
            kernel_text("\n") && kernel_log(0, "KERNEL_NET_DHCP_OK"));
    } else {
        (void)kernel_log(2, "KERNEL_NET_DHCP_TIMEOUT");
    }
    return 1;
}

// ====== kernel_net_info ======

int kernel_net_info(KERNEL_NET_INFO *out)
{
    if (!out)
        return 0;
    for (uint32_t i = 0; i < sizeof(*out); ++i)
        ((uint8_t *)out)[i] = 0U;
    if (!knet_state.Dev)
        return 1;
    for (uint32_t i = 0; i < 6U; ++i)
        out->Mac[i] = knet_state.Dev->Mac[i];
    out->Ip = knet_state.Ip;
    out->Mask = knet_state.Mask;
    out->Gateway = knet_state.Gateway;
    out->Dns = knet_state.Dns;
    out->Present = 1U;
    out->Configured = knet_state.Configured;
    out->LinkUp = (knet_state.Dev->Ops && knet_state.Dev->Ops->LinkUp)
        ? (uint32_t)(knet_state.Dev->Ops->LinkUp(knet_state.Dev->Context) != 0)
        : 0U;
    out->TxPackets = knet_state.Dev->TxPackets;
    out->RxPackets = knet_state.Dev->RxPackets;
    return 1;
}

// ====== kernel_net_arp_resolve ======

int kernel_net_arp_resolve(uint32_t target_ip, uint8_t mac_out[6], uint32_t timeout_ms)
{
    if (!knet_state.Dev || !knet_state.Configured || !mac_out || !target_ip)
        return KERNEL_NET_ERR_PARAM;

    if (target_ip == 0xFFFFFFFFU) {
        for (uint32_t i = 0; i < 6U; ++i)
            mac_out[i] = 0xFFU;
        return KERNEL_NET_OK;
    }

    uint32_t next_hop = ((target_ip & knet_state.Mask) == (knet_state.Ip & knet_state.Mask))
        ? target_ip
        : knet_state.Gateway;
    if (!next_hop)
        return KERNEL_NET_ERR_IO;

    if (arp_lookup_private(next_hop, mac_out))
        return KERNEL_NET_OK;

    uint8_t arp[28];
    uint32_t alen = knet_arp_build(
        arp, sizeof(arp), 1U,
        knet_state.Dev->Mac, knet_state.Ip,
        KNET_ZERO_MAC, next_hop
    );
    if (!alen)
        return KERNEL_NET_ERR_PARAM;

    uint32_t start = 0U;
    if (knet_state.HasClock)
        (void)kernel_clock_now(&knet_state.Clock, &start);
    uint32_t limit = ticks_for_ms_private(timeout_ms ? timeout_ms : 1000U);

    for (uint32_t attempt = 0; attempt < 2U; ++attempt) {
        (void)send_eth_private(KNET_BCAST_MAC, KNET_ETHERTYPE_ARP, arp, alen);
        for (uint32_t spin = 0; spin < 100000U; ++spin) {
            kernel_net_poll();
            if (arp_lookup_private(next_hop, mac_out))
                return KERNEL_NET_OK;
            if (knet_state.HasClock && kernel_clock_elapsed(&knet_state.Clock, start) >= limit)
                return KERNEL_NET_ERR_TIMEOUT;
        }
    }
    return KERNEL_NET_ERR_TIMEOUT;
}

// ====== kernel_net_send_ipv4 ======

int kernel_net_send_ipv4(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t payload_len)
{
    uint8_t dst_mac[6];
    int rc = kernel_net_arp_resolve(dst_ip, dst_mac, 1000U);
    if (rc != KERNEL_NET_OK)
        return rc;
    return send_ipv4_mac_private(dst_mac, knet_state.Ip, dst_ip, proto, payload, payload_len);
}

// ====== kernel_net_ping ======

int kernel_net_ping(uint32_t target_ip, uint32_t timeout_ms, uint32_t *rtt_ms_out)
{
    if (!knet_state.Dev || !knet_state.Configured || !target_ip)
        return KERNEL_NET_ERR_PARAM;

    uint8_t dst_mac[6];
    int rc = kernel_net_arp_resolve(target_ip, dst_mac, 1000U);
    if (rc != KERNEL_NET_OK)
        return rc;

    uint16_t id = 0x564FU;
    uint16_t seq = ++knet_state.NextPingSeq;
    uint8_t payload[16] = {'V','o','i','d','O','S','-','I','C','M','P','-','P','I','N','G'};
    uint8_t icmp[64];
    uint32_t ilen = knet_icmp_build_echo(icmp, sizeof(icmp), KNET_ICMP_ECHO_REQUEST, id, seq, payload, sizeof(payload));
    if (!ilen)
        return KERNEL_NET_ERR_PARAM;

    knet_state.PingWaitActive = 1U;
    knet_state.PingWaitId = id;
    knet_state.PingWaitSeq = seq;
    knet_state.PingReplyReceived = 0U;

    uint32_t start = 0U;
    if (knet_state.HasClock)
        (void)kernel_clock_now(&knet_state.Clock, &start);

    rc = send_ipv4_mac_private(dst_mac, knet_state.Ip, target_ip, KNET_IP_PROTO_ICMP, icmp, ilen);
    if (rc != KERNEL_NET_OK) {
        knet_state.PingWaitActive = 0U;
        return rc;
    }

    uint32_t limit = ticks_for_ms_private(timeout_ms ? timeout_ms : 2000U);
    for (uint32_t spin = 0; spin < 400000U; ++spin) {
        kernel_net_poll();
        if (knet_state.PingReplyReceived) {
            knet_state.PingWaitActive = 0U;
            if (rtt_ms_out)
                *rtt_ms_out = elapsed_ms_private(start);
            return KERNEL_NET_OK;
        }
        if (knet_state.HasClock && kernel_clock_elapsed(&knet_state.Clock, start) >= limit)
            break;
    }
    knet_state.PingWaitActive = 0U;
    return KERNEL_NET_ERR_TIMEOUT;
}

// ====== kernel_net_udp_exchange ======

int kernel_net_udp_exchange(
    uint32_t dst_ip,
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t *req,
    uint32_t req_len,
    uint8_t *resp_out,
    uint32_t resp_cap,
    uint32_t *resp_len_out,
    uint32_t timeout_ms
)
{
    if (!knet_state.Dev || !knet_state.Configured || !dst_ip || !req || !resp_out || !resp_len_out)
        return KERNEL_NET_ERR_PARAM;

    uint8_t dst_mac[6];
    int rc = kernel_net_arp_resolve(dst_ip, dst_mac, 1000U);
    if (rc != KERNEL_NET_OK)
        return rc;

    uint8_t udp[1480];
    uint32_t ulen = knet_udp_build(udp, sizeof(udp), knet_state.Ip, dst_ip, src_port, dst_port, req, req_len);
    if (!ulen)
        return KERNEL_NET_ERR_PARAM;

    knet_state.UdpWaitActive = 1U;
    knet_state.UdpWaitPort = src_port;
    knet_state.UdpWaitBuf = resp_out;
    knet_state.UdpWaitCap = resp_cap;
    knet_state.UdpWaitLen = 0U;
    knet_state.UdpWaitReceived = 0U;

    rc = send_ipv4_mac_private(dst_mac, knet_state.Ip, dst_ip, KNET_IP_PROTO_UDP, udp, ulen);
    if (rc != KERNEL_NET_OK) {
        knet_state.UdpWaitActive = 0U;
        return rc;
    }

    uint32_t start = 0U;
    if (knet_state.HasClock)
        (void)kernel_clock_now(&knet_state.Clock, &start);
    uint32_t limit = ticks_for_ms_private(timeout_ms ? timeout_ms : 2000U);

    for (uint32_t spin = 0; spin < 400000U; ++spin) {
        kernel_net_poll();
        if (knet_state.UdpWaitReceived) {
            *resp_len_out = knet_state.UdpWaitLen;
            knet_state.UdpWaitActive = 0U;
            return KERNEL_NET_OK;
        }
        if (knet_state.HasClock && kernel_clock_elapsed(&knet_state.Clock, start) >= limit)
            break;
    }
    knet_state.UdpWaitActive = 0U;
    return KERNEL_NET_ERR_TIMEOUT;
}
