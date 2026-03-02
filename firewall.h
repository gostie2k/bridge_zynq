/**
 * @file firewall.h
 * @brief Сетевой экран (пакетный фильтр) для порта 3 (радиоканал)
 *
 * Фильтрует исходящие пакеты (Ethernet → радио) по:
 *   - типу пакета (ARP, ICMP, TCP, UDP)
 *   - destination IP (с использованием маски, эталон = IP моста)
 */

#ifndef FIREWALL_H_
#define FIREWALL_H_

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>

/* ---------- Конфигурация фильтра ---------- */
typedef struct {
    uint32_t ip_mask;       /* Сетевая маска для фильтрации dst IP */
    uint8_t  arp_en;        /* Пропускать ARP:  0x01 = да, 0x00 = нет */
    uint8_t  icmp_en;       /* Пропускать ICMP: 0x01 = да, 0x00 = нет */
    uint8_t  tcp_en;        /* Пропускать TCP:  0x01 = да, 0x00 = нет */
    uint8_t  udp_en;        /* Пропускать UDP:  0x01 = да, 0x00 = нет */
} firewall_config_t;

/**
 * @brief Инициализация фильтра значениями по умолчанию.
 *        По умолчанию всё разрешено, маска = 0.0.0.0 (пропускает всё).
 */
void firewall_init(void);

/**
 * @brief Установить параметры фильтра.
 */
void firewall_set_config(uint32_t ip_mask,
                         uint8_t arp_en, uint8_t icmp_en,
                         uint8_t tcp_en, uint8_t udp_en);

/**
 * @brief Получить текущую конфигурацию фильтра (копия).
 */
firewall_config_t firewall_get_config(void);

/**
 * @brief Проверить, должен ли пакет быть пропущен через фильтр.
 *
 * @param p          Ethernet-фрейм (pbuf с eth_hdr в начале)
 * @param bridge_nif Указатель на bridge_netif (для получения эталонного IP)
 * @return 1 — пропустить, 0 — заблокировать
 */
int firewall_filter(struct pbuf *p, struct netif *bridge_nif);

#endif /* FIREWALL_H_ */
