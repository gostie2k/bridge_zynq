/* Packet filter for port 3 (radio). Filters Ethernet -> radio by packet
 * type and by dst IP under a mask, the reference being the bridge IP. */

#ifndef FIREWALL_H_
#define FIREWALL_H_

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>


typedef struct {
    uint32_t ip_mask;       /* mask for the dst IP check */
    uint8_t  arp_en;        /* 1 = pass */
    uint8_t  icmp_en;
    uint8_t  tcp_en;
    uint8_t  udp_en;
} firewall_config_t;

/* Defaults: everything passes, mask 0.0.0.0 */
void firewall_init(void);

void firewall_set_config(uint32_t ip_mask,
                         uint8_t arp_en, uint8_t icmp_en,
                         uint8_t tcp_en, uint8_t udp_en);

firewall_config_t firewall_get_config(void);

/* 1 = pass, 0 = block. p is an Ethernet frame, bridge_nif gives the ref IP. */
int firewall_filter(struct pbuf *p, struct netif *bridge_nif);

#endif /* FIREWALL_H_ */
