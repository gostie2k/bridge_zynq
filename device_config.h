#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>


typedef enum {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1,
} role_t;

struct device_config {
    uint8_t  role;            /* role_t */

    uint16_t device_id;       /* unique device ID (for addressed broadcast commands).
                               * Temporary: master=1, slave=2. Later - from flash UID. */

    uint8_t  ip[4];           /* own IP (bridge_netif) */
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  peer_ip[4];      /* peer address (RemoteAddr) */

    uint8_t  mac1[6];         /* GEM, port1 */
    uint8_t  mac3[6];         /* radio, vport3 */
    uint8_t  bridge_mac[6];   /* MAC bridge_netif */
};

/* The single config instance live at runtime. */
extern struct device_config current_config;

/* Fill cfg with defaults for the given role. */
void config_set_defaults(struct device_config *cfg, role_t role);

/* Fill current_config with defaults, role taken from IS_MASTER/IS_SLAVE. */
void config_init_defaults_from_build(void);

/* Request applying a new config (role/network change).
 * Safe from UDP callback: reinit is deferred to main loop. */
void device_request_reinit(const struct device_config *new_cfg);

/* Request saving current config to QSPI flash (survives reboot).
 * Safe from UDP callback: write deferred to main loop (blocks ~1 s). */
void device_request_save(void);

/* Request resetting flash config to factory (erase both copies). */
void device_request_erase(void);

#endif /* DEVICE_CONFIG_H_ */
