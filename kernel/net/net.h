#ifndef OS_KERNEL_NET_NET_H
#define OS_KERNEL_NET_NET_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/drivers/net/net_device.h"
#include "kernel/time/clock.h"

// ====== Rust protocol FFI ======

#define KNET_ETHERTYPE_IPV4     0x0800U
#define KNET_ETHERTYPE_ARP      0x0806U
#define KNET_IP_PROTO_ICMP      1U
#define KNET_IP_PROTO_TCP       6U
#define KNET_IP_PROTO_UDP       17U
#define KNET_ICMP_ECHO_REPLY    0U
#define KNET_ICMP_ECHO_REQUEST  8U
#define KNET_DHCP_CLIENT_PORT   68U
#define KNET_DHCP_SERVER_PORT   67U
#define KNET_DHCP_OFFER         2U
#define KNET_DHCP_ACK           5U

typedef struct {
    uint8_t   Dst[6];
    uint8_t   Src[6];
    uint16_t  EtherType;
    uint16_t  PayloadOffset;
    uint32_t  PayloadLen;
} KNET_ETH_INFO;

typedef struct {
    uint16_t  Opcode;
    uint8_t   SenderMac[6];
    uint8_t   TargetMac[6];
    uint32_t  SenderIp;
    uint32_t  TargetIp;
} KNET_ARP_INFO;

typedef struct {
    uint32_t  SrcIp;
    uint32_t  DstIp;
    uint8_t   Protocol;
    uint8_t   Ttl;
    uint16_t  HeaderLen;
    uint16_t  PayloadLen;
    uint16_t  Identification;
} KNET_IPV4_INFO;

typedef struct {
    uint8_t   IcmpType;
    uint8_t   Code;
    uint16_t  Id;
    uint16_t  Seq;
    uint16_t  PayloadLen;
} KNET_ICMP_INFO;

typedef struct {
    uint16_t  SrcPort;
    uint16_t  DstPort;
    uint16_t  PayloadLen;
    uint16_t  Reserved;
} KNET_UDP_INFO;

typedef struct {
    uint32_t  Xid;
    uint32_t  YourIp;
    uint32_t  ServerIp;
    uint32_t  SubnetMask;
    uint32_t  GatewayIp;
    uint32_t  DnsIp;
    uint32_t  LeaseSeconds;
    uint8_t   MsgType;
    uint8_t   Reserved[3];
} KNET_DHCP_REPLY;

int      knet_eth_parse(const uint8_t *frame, uint32_t frame_len, KNET_ETH_INFO *out);
uint32_t knet_eth_build(uint8_t *out, uint32_t cap, const uint8_t *dst, const uint8_t *src,
                        uint16_t ethertype, const uint8_t *payload, uint32_t payload_len);
int      knet_arp_parse(const uint8_t *payload, uint32_t len, KNET_ARP_INFO *out);
uint32_t knet_arp_build(uint8_t *out, uint32_t cap, uint16_t opcode, const uint8_t *sender_mac,
                        uint32_t sender_ip, const uint8_t *target_mac, uint32_t target_ip);
int      knet_ipv4_parse(const uint8_t *packet, uint32_t len, KNET_IPV4_INFO *out);
uint32_t knet_ipv4_build(uint8_t *out, uint32_t cap, uint32_t src_ip, uint32_t dst_ip,
                         uint8_t proto, uint16_t ident, const uint8_t *payload, uint32_t payload_len);
int      knet_icmp_parse(const uint8_t *payload, uint32_t len, KNET_ICMP_INFO *out);
uint32_t knet_icmp_build_echo(uint8_t *out, uint32_t cap, uint8_t icmp_type, uint16_t id,
                              uint16_t seq, const uint8_t *data, uint32_t data_len);
int      knet_udp_parse(const uint8_t *payload, uint32_t len, KNET_UDP_INFO *out);
uint32_t knet_udp_build(uint8_t *out, uint32_t cap, uint32_t src_ip, uint32_t dst_ip,
                        uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint32_t data_len);
uint32_t knet_dhcp_build_discover(uint8_t *out, uint32_t cap, const uint8_t *mac, uint32_t xid);
uint32_t knet_dhcp_build_request(uint8_t *out, uint32_t cap, const uint8_t *mac, uint32_t xid,
                                 uint32_t requested_ip, uint32_t server_ip);
int      knet_dhcp_parse_reply(const uint8_t *payload, uint32_t len, KNET_DHCP_REPLY *out);

// ====== Kernel Network Runtime API ======

typedef struct {
    uint8_t   Mac[6];
    uint16_t  Reserved;
    uint32_t  Ip;
    uint32_t  Mask;
    uint32_t  Gateway;
    uint32_t  Dns;
    uint32_t  Present;
    uint32_t  Configured;
    uint32_t  LinkUp;
    uint32_t  Pad;
    uint64_t  TxPackets;
    uint64_t  RxPackets;
} KERNEL_NET_INFO;

int  kernel_net_start(KERNEL_NET_DEVICE *dev, const KERNEL_CLOCK *clock);
void kernel_net_poll(void);
int  kernel_net_info(KERNEL_NET_INFO *out);
int  kernel_net_arp_resolve(uint32_t target_ip, uint8_t mac_out[6], uint32_t timeout_ms);
int  kernel_net_send_ipv4(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t payload_len);
int  kernel_net_ping(uint32_t target_ip, uint32_t timeout_ms, uint32_t *rtt_ms_out);
int  kernel_net_udp_exchange(
    uint32_t dst_ip,
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t *req,
    uint32_t req_len,
    uint8_t *resp_out,
    uint32_t resp_cap,
    uint32_t *resp_len_out,
    uint32_t timeout_ms
);

#endif
