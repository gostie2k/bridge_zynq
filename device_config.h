#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>

/* ===================================================================
 *  Runtime-конфиг устройства (роль + сеть).
 *
 *  Фаза 1 унификации: конфиг живёт в RAM. Дефолт при старте сидируется
 *  из компайл-тайм IS_MASTER/IS_SLAVE (см. config_init_defaults_from_build).
 *  Персистентность (QSPI) — отдельный этап позже.
 * =================================================================== */

typedef enum {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1,
} role_t;

struct device_config {
    uint8_t  role;            /* role_t */

    uint8_t  ip[4];           /* свой IP (bridge_netif) */
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  peer_ip[4];      /* адрес пира (RemoteAddr) */

    uint8_t  mac1[6];         /* GEM, port1 */
    uint8_t  mac3[6];         /* radio, vport3 */
    uint8_t  bridge_mac[6];   /* MAC bridge_netif */
};

/* Единственный экземпляр конфига, действующий в рантайме. */
extern struct device_config current_config;

/* Заполнить cfg дефолтами для заданной роли. */
void config_set_defaults(struct device_config *cfg, role_t role);

/* Заполнить current_config дефолтами, роль берётся из IS_MASTER/IS_SLAVE. */
void config_init_defaults_from_build(void);

#endif /* DEVICE_CONFIG_H_ */
