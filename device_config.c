#include "device_config.h"
#include "platform_config.h"   /* IS_MASTER / IS_SLAVE - default role seed */
#include <string.h>

struct device_config current_config;

void config_set_defaults(struct device_config *cfg, role_t role)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->role = (uint8_t)role;
    cfg->device_id = DEVICE_ID_DEFAULT;   /* per-device ID from platform_config.h (not from role) */

    /* Common to both roles */
    cfg->netmask[0] = 255; cfg->netmask[1] = 255; cfg->netmask[2] = 255; cfg->netmask[3] = 0;
    cfg->gateway[0] = 192; cfg->gateway[1] = 168; cfg->gateway[2] = 250; cfg->gateway[3] = 1;

    if (role == ROLE_MASTER) {
        cfg->ip[0]      = 192; cfg->ip[1]      = 168; cfg->ip[2]      = 250; cfg->ip[3]      = 11;
        cfg->peer_ip[0] = 192; cfg->peer_ip[1] = 168; cfg->peer_ip[2] = 250; cfg->peer_ip[3] = 22;

        const uint8_t m1[6] = {0x00, 0x1A, 0x35, 0xAA, 0xBB, 0xCC};
        const uint8_t m3[6] = {0x00, 0x1A, 0x35, 0xDD, 0xEE, 0x03};
        const uint8_t mb[6] = {0x00, 0x1A, 0x35, 0xAA, 0xBB, 0xCE};
        memcpy(cfg->mac1, m1, 6);
        memcpy(cfg->mac3, m3, 6);
        memcpy(cfg->bridge_mac, mb, 6);
    } else { /* ROLE_SLAVE */
        cfg->ip[0]      = 192; cfg->ip[1]      = 168; cfg->ip[2]      = 250; cfg->ip[3]      = 21;
        cfg->peer_ip[0] = 192; cfg->peer_ip[1] = 168; cfg->peer_ip[2] = 250; cfg->peer_ip[3] = 10;

        const uint8_t m1[6] = {0x00, 0x0A, 0x35, 0xAA, 0xBB, 0xCC};
        const uint8_t m3[6] = {0x00, 0x0A, 0x35, 0xDD, 0xEE, 0x03};
        const uint8_t mb[6] = {0x00, 0x0A, 0x35, 0xAA, 0xBB, 0xCE};
        memcpy(cfg->mac1, m1, 6);
        memcpy(cfg->mac3, m3, 6);
        memcpy(cfg->bridge_mac, mb, 6);
    }
}

void config_init_defaults_from_build(void)
{
#if defined(IS_MASTER) && !defined(IS_SLAVE)
    config_set_defaults(&current_config, ROLE_MASTER);
#else
    config_set_defaults(&current_config, ROLE_SLAVE);
#endif
}
