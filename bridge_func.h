/* IEEE 802.1D MAC bridge: 3 ports, 1 virtual netif. phy xemac <-> port 1. */

/*
 * Copyright (c) 2017 KNK.
 * All rights reserved.


 * This file is part of the PKRV MODEM Project.
 *
 * Author: KNK <@.>
 *
 */

#include "netif/bridgeif.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "platform_config.h"
#include "xemacps.h"
#include "netif/xadapter.h"
#include "firewall.h"
#include "modem.h"
#include "device_config.h"



#ifndef BRIDGE_FUNC_H_
#define BRIDGE_FUNC_H_

#define DEB_FUNC  0

/* filled at runtime from current_config in net_init; ethaddr set before netif_add */
extern bridgeif_initdata_t bridge_data;


typedef struct {
    u8_t used;
    u8_t port_idx;
    u32_t timestamp;
    struct eth_addr addr;
} bridgeif_fdb_dynamic_entry_t;

typedef struct {
    u16_t max_fdb_entries;
    bridgeif_fdb_dynamic_entry_t *fdb;
} bridgeif_fdb_dynamic_t;

/* mac1=GEM, mac3=radio live in current_config */





void print_dynamic_fdb(struct netif *bridgeif);

void* bridgeif_get_dynamic_fdb(struct netif *bridgeif);

err_t vport2_init(struct netif *netif);
err_t vport2_linkoutput(struct netif *netif, struct pbuf *p);

err_t vport3_init(struct netif *netif);
err_t vport3_linkoutput(struct netif *netif, struct pbuf *p);


err_t udpif2_init(struct netif *netif);
err_t udpif2_linkoutput(struct netif *netif, struct pbuf *p);

err_t eth_inp(struct pbuf *p, struct netif *netif);

void net_init();



#endif // BRIDGE_FUNC_H_
