#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>


typedef enum {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1,
} role_t;

struct device_config {
    uint8_t  role;            /* role_t */
    uint16_t device_id;       /* for addressed broadcast cmds */
    uint8_t  ip[4];           /* own IP (bridge_netif) */
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  peer_ip[4];      /* RemoteAddr */
    uint8_t  mac1[6];         /* GEM, port1 */
    uint8_t  mac3[6];         /* radio, vport3 */
    uint8_t  bridge_mac[6];
};

extern struct device_config current_config;

void config_set_defaults(struct device_config *cfg, role_t role);
void config_init_defaults_from_build(void);   /* role from IS_MASTER/IS_SLAVE */

/* Safe from a UDP callback: the work is deferred to the main loop */
void device_request_reinit(const struct device_config *new_cfg);
void device_request_save(void);               /* blocks ~1 s when it runs */
void device_request_erase(void);

#endif /* DEVICE_CONFIG_H_ */
