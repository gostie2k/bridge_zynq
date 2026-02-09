/**
 * @file
 * IEEE 802.1D MAC Bridge with 3 ports & 1 virtual Netif
 *    - (phy xemac) <-> port 1
 *    -
 */

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



#ifndef BRIDGE_FUNC_H_
#define BRIDGE_FUNC_H_

#define DEB_FUNC  0

#ifdef IS_MASTER
static const bridgeif_initdata_t bridge_data =
  BRIDGEIF_INITDATA1(3, 1024, 16,
    ETH_ADDR(0x01,0x0A,0x35,0xAA,0xBB,0xCE));
#endif

#ifdef IS_SLAVE
static const bridgeif_initdata_t bridge_data =
  BRIDGEIF_INITDATA1(3, 1024, 16,
    ETH_ADDR(0x00,0x0A,0x35,0xAA,0xBB,0xCE));
#endif


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

#ifdef IS_MASTER
 static u8_t mac1[6] = {0x01,0x0A,0x35,0xAA,0xBB,0xCC};
 static  u8_t mac2[6] = {0x01,0x0A,0x35,0xDD,0xEE,0x02};
 static u8_t mac3[6] = {0x01,0x0A,0x35,0xDD,0xEE,0x03};
 static u8_t mac4[6] = {0x01,0x0A,0x35,0xDD,0xEE,0x04};
#endif

#ifdef IS_SLAVE
 static u8_t mac1[6] = {0x00,0x0A,0x35,0xAA,0xBB,0xCC};
 static u8_t mac2[6] = {0x00,0x0A,0x35,0xDD,0xEE,0x02};
 static u8_t mac3[6] = {0x00,0x0A,0x35,0xDD,0xEE,0x03};
 static u8_t mac4[6] = {0x00,0x0A,0x35,0xDD,0xEE,0x04};
#endif



void* bridgeif_get_dynamic_fdb(struct netif *bridgeif);

void print_dynamic_fdb(struct netif *bridgeif);

err_t vport2_init(struct netif *netif);
err_t vport2_linkoutput(struct netif *netif, struct pbuf *p);

err_t vport3_init(struct netif *netif);
err_t vport3_linkoutput(struct netif *netif, struct pbuf *p);

err_t udpif2_init(struct netif *netif);
err_t udpif2_linkoutput(struct netif *netif, struct pbuf *p);

err_t eth_inp(struct pbuf *p, struct netif *netif);

void net_init();

//static err_t udpif2_demux_input(struct pbuf *p, struct netif *netif);


#endif // BRIDGE_FUNC_H_
