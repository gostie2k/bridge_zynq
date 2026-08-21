/**
 * @file firewall.h
 * @brief Firewall (packet filter) for port 3 (radio channel)
 *
 * Filters outgoing packets (Ethernet -> radio) by:
 *   - packet type (ARP, ICMP, TCP, UDP)
 *   - destination IP (masked, reference = bridge IP)
 */

#ifndef FIREWALL_H_
#define FIREWALL_H_

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>

/* ---------- Filter config ---------- */
typedef struct {
    uint32_t ip_mask;       /* Network mask for dst IP filtering */
    uint8_t  arp_en;        /* Pass ARP:  0x01 = yes, 0x00 = no */
    uint8_t  icmp_en;       /* Pass ICMP: 0x01 = yes, 0x00 = no */
    uint8_t  tcp_en;        /* Pass TCP:  0x01 = yes, 0x00 = no */
    uint8_t  udp_en;        /* Pass UDP:  0x01 = yes, 0x00 = no */
} firewall_config_t;

/**
 * @brief Init filter with defaults.
 *        By default everything allowed, mask = 0.0.0.0 (pass all).
 */
void firewall_init(void);

/**
 * @brief Set filter parameters.
 */
void firewall_set_config(uint32_t ip_mask,
                         uint8_t arp_en, uint8_t icmp_en,
                         uint8_t tcp_en, uint8_t udp_en);

/**
 * @brief Get current filter config (copy).
 */
firewall_config_t firewall_get_config(void);

/**
 * @brief Check whether a packet should pass the filter.
 *
 * @param p          Ethernet frame (pbuf with eth_hdr at start)
 * @param bridge_nif Pointer to bridge_netif (for the reference IP)
 * @return 1 - pass, 0 - block
 */
int firewall_filter(struct pbuf *p, struct netif *bridge_nif);

#endif /* FIREWALL_H_ */
