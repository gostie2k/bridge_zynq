#include "bridge_func.h"
#include "lwip/pbuf.h"
#include "cmd_buf.h"
#include "radio_stream.h"
//extern struct netif udp_netif2;
extern struct netif port1_bridge;
//extern struct netif port2_bridge;
extern struct netif port3_bridge;
extern struct netif bridge_netif;

extern void cmd_push(cmd_buf** _buffer, uint8_t* _data, uint16_t _size);
extern cmd_buf* buf_cmd;

extern u16 deb_cnt_rf;


#ifdef BRIDGE



/*forwarding database table (FDB) of bridge printing*/
void print_dynamic_fdb(struct netif *bridgeif) {
    bridgeif_fdb_dynamic_t *db =
        (bridgeif_fdb_dynamic_t*)bridgeif_get_dynamic_fdb(bridgeif);
    u16_t count = bridgeif_get_dynamic_fdb_count(bridgeif);
    xil_printf("\n\r--- Dynamic FDB entries ---\n\r");
    for (int i = 0; i < count; i++) {
        bridgeif_fdb_dynamic_entry_t *e = &db->fdb[i];
        if (e->used) {
            struct eth_addr *a = &e->addr;
            xil_printf("[%3d] %02X:%02X:%02X:%02X:%02X:%02X -> port %d\n\r",
                       i,
                       a->addr[0], a->addr[1], a->addr[2],
                       a->addr[3], a->addr[4], a->addr[5],
                       e->port_idx);
        }
    }
    xil_printf("--- end dynamic FDB ---\n\r");
}




/* --- PORT2 --- */
/*
err_t vport2_init(struct netif *netif) {
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac2, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_BROADCAST
                 | NETIF_FLAG_ETHARP
                 | NETIF_FLAG_ETHERNET
                 | NETIF_FLAG_IGMP
                 | NETIF_FLAG_LINK_UP;
    netif->linkoutput = vport2_linkoutput;
    return ERR_OK;
}

err_t vport2_linkoutput(struct netif *netif, struct pbuf *p) {

	struct pbuf *pp;
	pp = pbuf_alloc(PBUF_TRANSPORT, p->len, PBUF_RAM);
	err_t out = ERR_OK;
	out = pbuf_copy(pp, p);

	if (DEB_FUNC)
	{
	       struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
	        xil_printf("[port2_bridge] ethernet_input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d  \n\r",
	       (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
	       (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
	       (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
	       (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
		    lwip_htons(ethhdr->type), p->tot_len  );
	}

    out = ERR_OK;
	out = ethernet_input(pp, &udp_netif2);

	//out = ethernet_input(p, &udp_netif2);
	//pbuf_free(p);

	return out;
}

*/

/* --- PORT2 --- */
err_t vport3_init(struct netif *netif) {



    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac3, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

    		//NETIF_FLAG_BROADCAST
             //    | NETIF_FLAG_ETHARP
             //    | NETIF_FLAG_ETHERNET
             //    | NETIF_FLAG_IGMP
             //    | NETIF_FLAG_LINK_UP;

    netif->linkoutput = vport3_linkoutput;
    return ERR_OK;
}

 err_t vport3_linkoutput(struct netif *netif, struct pbuf *p) {

    /* ===== СЕТЕВОЙ ЭКРАН: фильтрация перед отправкой в радиоканал ===== */
    if (!firewall_filter(p, &bridge_netif)) {
        /* Пакет не прошёл фильтр — не отправляем в радио */
        return ERR_OK;
    }

    deb_cnt_rf++;
    return radio_linkoutput(netif, p);
}


/*
err_t udpif2_init(struct netif *netif) {
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac4, ETH_HWADDR_LEN);
    netif->mtu   = 1500;
    netif->flags = NETIF_FLAG_BROADCAST
                 | NETIF_FLAG_ETHARP
                 | NETIF_FLAG_ETHERNET
                 | NETIF_FLAG_LINK_UP;

    netif->output     = etharp_output;
    netif->linkoutput = udpif2_linkoutput;
    return ERR_OK;
}


err_t udpif2_linkoutput(struct netif *netif, struct pbuf *p) {

	if (DEB_FUNC)
	{
       struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
        xil_printf("[udp_netif2]   ethernet_input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d  \n\r",
       (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
       (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
       (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
       (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
	   lwip_htons(ethhdr->type) , p->tot_len  );
	}

	err_t out;
    out = port2_bridge.input(p, &port2_bridge);
	return out;
}




err_t eth_inp(struct pbuf *p, struct netif *netif)
{
//Bridge network interface

	if (DEB_FUNC)
	{
	       struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
	        xil_printf("[bridge_netif] ethernet_input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d  \n\r",
	       (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
	       (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
	       (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
	       (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
		    lwip_htons(ethhdr->type), p->tot_len  );
	}

	pbuf_free(p);
	return ERR_OK;
}


*/

void net_init()
{
    ip4_addr_t ipb, nmb, gwb, ip2, nm2, gw2;


   #ifdef IS_MASTER
     IP4_ADDR(&ipb, 192,168,250,11);
   #endif
   #ifdef IS_SLAVE
     IP4_ADDR(&ipb, 192,168,250,21);
   #endif


     IP4_ADDR(&nmb, 255,255,255,0);
     IP4_ADDR(&gwb, 192,168,250,1);

     bridge_netif.state = (bridgeif_initdata_t *) &bridge_data;

     netif_add(&bridge_netif,
               &ipb, &nmb, &gwb,
               (void*)&bridge_data,
               bridgeif_init, ethernet_input); //eth_inp);

     bridge_netif.flags|= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_BROADCAST;



     xemac_add(&port1_bridge,
               NULL, NULL, NULL,
               mac1, PLATFORM_EMAC_BASEADDR);

     port1_bridge.flags|=NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;


     //set promiscuous mode
     u32 Reg = XEmacPs_ReadReg(PLATFORM_EMAC_BASEADDR,
        XEMACPS_NWCFG_OFFSET);

      XEmacPs_WriteReg(PLATFORM_EMAC_BASEADDR, XEMACPS_NWCFG_OFFSET,
         Reg|0x00000010);



      netif_add(&port3_bridge,
                     NULL, NULL, NULL,
                     NULL, vport3_init, NULL);//ethernet_input);
      port3_bridge.flags|= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;



      bridgeif_add_port(&bridge_netif, &port1_bridge);
      bridgeif_add_port(&bridge_netif, &port3_bridge);


     netif_set_up (&bridge_netif);

     netif_set_link_up(&port3_bridge);
     netif_set_up(&port3_bridge);netif_set_default(&bridge_netif);


     netif_set_up(&port1_bridge);

     netif_set_default(&bridge_netif);


}

#endif
