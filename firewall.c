/**
 * @file firewall.c
 * @brief Реализация сетевого экрана для порта 3 (радиоканал)
 */

#include "firewall.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/ip4.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"
#include "xil_printf.h"
#include <string.h>

/* Глобальная конфигурация фильтра */
static firewall_config_t fw_cfg;


#define FW_BLOCK(reason) do { \
    xil_printf("[FW] BLOCKED: %s (len=%d)\r\n", reason, p->tot_len); \
    return 0; \
} while(0)


/* ========== API ========== */

void firewall_init(void)
{
    /* По умолчанию: всё разрешено, маска 0 — не фильтруем по IP */
    fw_cfg.ip_mask  = 0x00000000;
    fw_cfg.arp_en   = 0x01;
    fw_cfg.icmp_en  = 0x01;
    fw_cfg.tcp_en   = 0x01;
    fw_cfg.udp_en   = 0x01;
}

void firewall_set_config(uint32_t ip_mask,
                         uint8_t arp_en, uint8_t icmp_en,
                         uint8_t tcp_en, uint8_t udp_en)
{
    fw_cfg.ip_mask  = ip_mask;
    fw_cfg.arp_en   = arp_en;
    fw_cfg.icmp_en  = icmp_en;
    fw_cfg.tcp_en   = tcp_en;
    fw_cfg.udp_en   = udp_en;

    xil_printf("[FW] Config updated: mask=%d.%d.%d.%d  ARP=%d ICMP=%d TCP=%d UDP=%d\r\n",
               (ip_mask >> 24) & 0xFF, (ip_mask >> 16) & 0xFF,
               (ip_mask >> 8) & 0xFF,  ip_mask & 0xFF,
               arp_en, icmp_en, tcp_en, udp_en);
}

firewall_config_t firewall_get_config(void)
{
    return fw_cfg;
}

/* ========== Фильтрация ========== */

/**
 * Логика фильтрации:
 *
 * 1. EtherType == 0x0806 (ARP):
 *    → пропускаем если arp_en == 0x01
 *
 * 2. EtherType == 0x0800 (IPv4):
 *    a) Проверяем destination IP по маске:
 *       (dst_ip & mask) == (bridge_ip & mask)
 *       Если mask == 0 — проверка пропускается (пропускаем всё).
 *       Broadcast (dst = 255.255.255.255) всегда пропускается.
 *    b) Проверяем протокол:
 *       - ICMP (1)  → icmp_en
 *       - TCP  (6)  → tcp_en
 *       - UDP  (17) → udp_en
 *       - иное      → блокируем
 *
 * 3. Все остальные EtherType → блокируем
 */
int firewall_filter(struct pbuf *p, struct netif *bridge_nif)
{
    if (p == NULL || p->len < sizeof(struct eth_hdr)) {
    	FW_BLOCK("too short"); /* слишком короткий — блокируем */
    }

    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    uint16_t eth_type = lwip_ntohs(ethhdr->type);

    /* --- ARP --- */
    if (eth_type == ETHTYPE_ARP) {
        if (fw_cfg.arp_en != 0x01) FW_BLOCK("ARP disabled");
        return 1;  // ARP разрешён
    }

    /* --- IPv4 --- */
    if (eth_type == ETHTYPE_IP) {
        if (p->len < (sizeof(struct eth_hdr) + 20))
            FW_BLOCK("IP hdr too short");

        uint8_t *ip_hdr = (uint8_t *)p->payload + sizeof(struct eth_hdr);

        uint32_t dst_ip;
        memcpy(&dst_ip, ip_hdr + 16, 4);

        if (fw_cfg.ip_mask != 0) {
            uint32_t mask_n = lwip_htonl(fw_cfg.ip_mask);
            if (dst_ip != 0xFFFFFFFF) {
                uint32_t bridge_ip_n = ip4_addr_get_u32(netif_ip4_addr(bridge_nif));
                if ((dst_ip & mask_n) != (bridge_ip_n & mask_n))
                    FW_BLOCK("IP mask mismatch");
            }
        }

        uint8_t protocol = ip_hdr[9];
        switch (protocol) {
            case 1:  if (fw_cfg.icmp_en != 0x01) FW_BLOCK("ICMP disabled"); return 1;
            case 6:  if (fw_cfg.tcp_en  != 0x01) FW_BLOCK("TCP disabled");  return 1;
            case 17: if (fw_cfg.udp_en  != 0x01) FW_BLOCK("UDP disabled");  return 1;
            default: FW_BLOCK("unknown IP proto");
        }
    }

    FW_BLOCK("non-IPv4/ARP");
}
