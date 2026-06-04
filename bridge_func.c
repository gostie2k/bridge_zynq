#include "bridge_func.h"
#include "lwip/pbuf.h"
#include "cmd_buf.h"
//extern struct netif udp_netif2;
extern struct netif port1_bridge;
//extern struct netif port2_bridge;
extern struct netif port3_bridge;
extern struct netif bridge_netif;

extern void cmd_push(cmd_buf** _buffer, uint8_t* _data, uint16_t _size);
extern cmd_buf* buf_cmd;

extern u16 deb_cnt_rf;
//extern bool tx_seg_on;


extern struct ppos_tx tx_chan0;
extern struct ppos_rx rx_chan0;

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

    //netif->output = vport3_output;

    return ERR_OK;
}


//static err_t vport3_output(struct netif *netif, struct pbuf *p,
//		const ip_addr_t *ipaddr)
//{
//
//	/* resolve hardware address, then send (or queue) packet */
//	return etharp_output(netif, p, ipaddr);
//}




err_t vport3_linkoutput(struct netif *netif, struct pbuf *p) {

	 struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
     err_t out = ERR_OK;



    // if ((p->tot_len != p->len)||(p->tot_len>=1500))
	if (DEB_FUNC)
	{
	       //struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
	        xil_printf("vport3_linkoutput: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  tot_len: %4d len: %4d \n\r",
	       (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
	       (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
	       (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
	       (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
		   lwip_htons(ethhdr->type), p->tot_len, p->len);

/*
           int row = 0;
	       for (int i=0;i< p->tot_len; i++)
	       {
	    	   xil_printf("%02X ", *(  (char*) p->payload + i)      );

	       row++;

	       if (row == 16)
	       {
	    	   xil_printf("\n\r" );
	    	   row = 0;
	       }

	       }

	       xil_printf("\n\r" );

	*/
	}



	if (tx_chan0.creg0.inside.pkt_len == 576)
	{
	   deb_cnt_rf++;
	   return radio_linkoutput(netif, p);
	}



	if (tx_chan0.creg0.inside.pkt_len == 4800)
	{




	    cmd_push(&buf_cmd,(uint8_t*) p->payload, p->len);
	    deb_cnt_rf++;
	    //pbuf_free(p);
		return out;
	}






//	 if (tx_seg_on)
//	 {
	    /* ===== —≈“≈¬ќ… Ё –јЌ: фильтраци€ перед отправкой в радиоканал ===== */
	   // if (!firewall_filter(p, &bridge_netif)) {
	        /* ѕакет не прошЄл фильтр Ч не отправл€ем в радио */
	   //     return ERR_OK;
	   // }
//	    deb_cnt_rf++;
//	    return radio_linkoutput(netif, p);
//	 }
//	 else
//	 {
//	    cmd_push(&buf_cmd,(uint8_t*) p->payload, p->len);
 //       deb_cnt_rf++;
 //       //pbuf_free(p);
//	    return out;
//	 }


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

     bridge_netif.flags|= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;



     //netif_set_addr (&bridge_netif, &ipb, &nmb, &gwb);











     xemac_add(&port1_bridge,
               NULL, NULL, NULL,
               mac1, PLATFORM_EMAC_BASEADDR);

     port1_bridge.flags|=NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

     //port1_bridge.name[0] = 'P';
     //port1_bridge.name[1] = '1';

     //netif_set_default(&port1_bridge);


     //set promiscuous mode
     u32 Reg = XEmacPs_ReadReg(PLATFORM_EMAC_BASEADDR,
      		XEMACPS_NWCFG_OFFSET);

      XEmacPs_WriteReg(PLATFORM_EMAC_BASEADDR, XEMACPS_NWCFG_OFFSET,
   			   Reg|0x00000010);


     // netif_add(&port2_bridge,
     //           NULL, NULL, NULL,
     //           NULL, vport2_init, NULL);//ethernet_input);
     // port2_bridge.name[0] = 'P';
     // port2_bridge.name[1] = '2';
     // port2_bridge.flags |= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
     // netif_set_link_up(&port2_bridge);
     // netif_set_up(&port2_bridge);


      netif_add(&port3_bridge,
                     NULL, NULL, NULL,
                     NULL, vport3_init, NULL);//ethernet_input);
      //port3_bridge.name[0] = 'P';
      //port3_bridge.name[1] = '3';
      port3_bridge.flags|= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;






      bridgeif_add_port(&bridge_netif, &port1_bridge);
     // bridgeif_add_port(&bridge_netif, &port2_bridge);
      bridgeif_add_port(&bridge_netif, &port3_bridge);






     netif_set_up (&bridge_netif);
     //netif_set_link_up (&bridge_netif);

     netif_set_link_up(&port3_bridge);
     netif_set_up(&port3_bridge);

     //netif_set_link_up(&port1_bridge);
     netif_set_up(&port1_bridge);






/*
#ifdef IS_OLD_PAR
#ifdef IS_MASTER
     IP4_ADDR(&ip2, 169,254,206,11);
#endif
#ifdef IS_SLAVE
     IP4_ADDR(&ip2, 169,254,206,21);
#endif
#endif


#ifndef IS_OLD_PAR
#ifdef IS_MASTER
     IP4_ADDR(&ip2, 192,168,250,13);
#endif
#ifdef IS_SLAVE
     IP4_ADDR(&ip2, 192,168,250,23);
#endif
#endif

*/

 //    IP4_ADDR(&nm2, 255,255,255,0);
 //    IP4_ADDR(&gw2, 169,254,206,1);
     //IP4_ADDR(&gw2, 192,168,250,1);
  //   netif_add(&udp_netif2,
  //             &ip2, &nm2, &gw2,
  //             NULL, udpif2_init,ethernet_input);

 //    udp_netif2.name[0] = 'V';
 //    udp_netif2.name[1] = '1';

//     netif_set_link_up(&udp_netif2);
//     netif_set_up(&udp_netif2);



}

#endif
