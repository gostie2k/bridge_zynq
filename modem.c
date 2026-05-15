#include "modem.h"
#include "lwip/stats.h"
#include "xil_printf.h"
#include <stdlib.h>

extern struct ppos_tx tx_chan0;
extern struct ppos_rx rx_chan0;
extern struct ad9361_rf_phy *ad9361_phy;
extern struct ppos_par ppos;

extern  struct udp_pcb *debug_pcb;
extern  struct netif bridge_netif;

struct stat_modem* stat_modem_init()
{
    struct stat_modem* status_modem;
    status_modem = malloc(sizeof(struct stat_modem));
    return status_modem;
}


/* ========== lwIP stats  ========== */

/**
 * Fill stats_resp_t with current data from lwIP.
 *
 * Requirements in lwipopts.h:
 *   #define LWIP_STATS      1
 *   #define MEM_STATS       1
 *   #define MEMP_STATS      1
 *   #define LINK_STATS      1
 */
void modem_fill_stats(stats_resp_t *resp)
{
    if (resp == NULL) return;

    resp->head[0] = CMD_PROTO_MAGIC;
    resp->head[1] = CMD_GET_STATS;

#if LWIP_STATS && MEM_STATS
    resp->mem_used = (uint32_t)lwip_stats.mem.used;
    resp->mem_max  = (uint32_t)lwip_stats.mem.max;
    resp->mem_err  = (uint32_t)lwip_stats.mem.err;
#else
    resp->mem_used = resp->mem_max = resp->mem_err = 0;
#endif

#if LWIP_STATS && MEMP_STATS
    const struct stats_mem *pbuf_s = lwip_stats.memp[MEMP_PBUF];
    if (pbuf_s) {
        resp->pbuf_used = (uint32_t)pbuf_s->used;
        resp->pbuf_max  = (uint32_t)pbuf_s->max;
        resp->pbuf_err  = (uint32_t)pbuf_s->err;
    } else {
        resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    }

    const struct stats_mem *pool_s = lwip_stats.memp[MEMP_PBUF_POOL];
    if (pool_s) {
        resp->pool_used = (uint32_t)pool_s->used;
        resp->pool_max  = (uint32_t)pool_s->max;
        resp->pool_err  = (uint32_t)pool_s->err;
    } else {
        resp->pool_used = resp->pool_max = resp->pool_err = 0;
    }
#else
    resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    resp->pool_used = resp->pool_max = resp->pool_err = 0;
#endif

#if LWIP_STATS && LINK_STATS
    resp->link_drop = (uint32_t)lwip_stats.link.drop;
#else
    resp->link_drop = 0;
#endif
}

void modem_print_stats(void)
{
    stats_resp_t resp;
    modem_fill_stats(&resp);

    xil_printf("=== lwIP stats ===\r\n");
    xil_printf("  Heap:      used=%u  max=%u  err=%u\r\n",
               resp.mem_used, resp.mem_max, resp.mem_err);
    xil_printf("  PBUF hdr:  used=%u  max=%u  err=%u\r\n",
               resp.pbuf_used, resp.pbuf_max, resp.pbuf_err);
    xil_printf("  PBUF pool: used=%u  max=%u  err=%u\r\n",
               resp.pool_used, resp.pool_max, resp.pool_err);
    xil_printf("  Link drop: %u\r\n", resp.link_drop);
}


/* ========== SPI Engine functions (from video_debug version) ========== */



void spi_engine_ad9361_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_AD9361_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 0;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;



    struct no_os_spi_init_param no_os_spi_init= {0};;
    no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    no_os_spi_init.mode=1;
    no_os_spi_init.device_id = 0;
    no_os_spi_init.chip_select = 0;
    no_os_spi_init.max_speed_hz = 50000000;


    spi_engine_init(spi_eng, &no_os_spi_init);

    return;
}



void spi_engine_npu_com_board_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;


    struct no_os_spi_init_param no_os_spi_init={0};
    no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    no_os_spi_init.mode=NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select =  NPU_COM_BOARD_CS;
    no_os_spi_init.max_speed_hz = 25000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    spi_engine_set_speed(*spi_eng, 1000000);
    //MAX7301AAX+
    uint8_t tx_buf[2];

    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    //00 -not use, 01 - output, 10- Schmitt logic input, 11 - Schmitt logic input with pullup
    tx_buf[0] = 0x09; // Адрес регистра конфигурации портов P[7:4]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0A; // Адрес регистра конфигурации портов P[11:8]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0B; // Адрес регистра конфигурации портов P[15:12]
    tx_buf[1] = 0xfd; // Данные: 11 11 11 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0C; // Адрес регистра конфигурации портов P[19:16]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0D; // Адрес регистра конфигурации портов P[23:20]
    tx_buf[1] = 0x57; // Данные: 01 01 01 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0E; // Адрес регистра конфигурации портов P[27:24]
    tx_buf[1] = 0xD5; // Данные: 11 01 01 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0F; // Адрес регистра конфигурации портов P[27:24]
    tx_buf[1] = 0x5D; // Данные: 01 01 11 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x2c; // Запись портов Pх12]
    tx_buf[1] = 0x00; // Данные:
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    //tx_buf[0] = 0x55; // Запись портов P[28:21]
    //tx_buf[1] = 0x00; // Данные:
    //spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    //usleep(100);


    tx_buf[0] = 0x04; // Адрес регистра конфигурации
    tx_buf[1] = 0x01; // Данные: Shutdown Control = Normal Operation
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);




    return;


}



void spi_engine_npu_pow_board_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;


    struct no_os_spi_init_param no_os_spi_init;
    no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    no_os_spi_init.mode=NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select =  NPU_POW_BOARD_CS;
    no_os_spi_init.max_speed_hz = 25000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    spi_engine_set_speed(*spi_eng, 1000000);

    //MAX7301AAX+
    uint8_t tx_buf[2];

    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    //00 -вход, 01 - вход с подтяжкой, 10- выход, 11 - не используется
    tx_buf[0] = 0x09; // Адрес регистра конфигурации портов P[7:4]
    tx_buf[1] = 0xf5; // Данные: 11 11 01 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0A; // Адрес регистра конфигурации портов P[11:8]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0B; // Адрес регистра конфигурации портов P[15:12]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0C; // Адрес регистра конфигурации портов P[19:16]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0D; // Адрес регистра конфигурации портов P[23:20]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0E; // Адрес регистра конфигурации портов P[27:24]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0F; // Адрес регистра конфигурации портов P[27:24]
    tx_buf[1] = 0xff; // Данные: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x40; // Запись портов P[7:4]
    tx_buf[1] = 0x03;
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x04; // Адрес регистра конфигурации
    tx_buf[1] = 0x01; // Данные: Shutdown Control = Normal Operation
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    return;
}



 void spi_engine_npu_com_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{

	uint8_t tx_buf[2];
	tx_buf[0] = 0xC4;
	tx_buf[1] = 0x00;
	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

	status_npu->tx_panel_1.word = tx_buf[1]; // !!!!!!!!!!!!!!! м.б. 7:0 -> 0:7

	tx_buf[0] = 0xCD;
	tx_buf[1] = 0x00;
	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

	status_npu->tx_panel_2.word = tx_buf[1]; // !!!!!!!!!!!!!!!!!! м.б. 7:0 -> 0:7

}


 void spi_engine_npu_com_board_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{

   uint8_t tx_buf[2];
   if ( com_dev ==TX_PANEL_1 ) // Питание панели передающей №1
   {
		tx_buf[0] = 0x2C;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==TX_PANEL_2 ) // Питание панели передающей №2
   {
		tx_buf[0] = 0x35;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_1 ) // Питание панели приемной №1
   {
		tx_buf[0] = 0x36;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_2 ) // Питание панели приемной №2
   {
		tx_buf[0] = 0x37;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_3 ) // Питание панели приемной №3
   {
		tx_buf[0] = 0x38;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_4 ) // Питание панели приемной №4
   {
		tx_buf[0] = 0x39;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

}



 //void spi_engine_npu_pow_board_set_f_diap(struct no_os_spi_desc*  spi_eng,  pow_cntrl_type typ )
 void spi_engine_npu_pow_board_set_f_diap(struct no_os_spi_desc*  spi_eng_com, struct no_os_spi_desc*  spi_eng,  pow_cntrl_type typ )

 {
        uint8_t tx_buf[2];
		tx_buf[0] =  0x40;
		tx_buf[1] =   typ;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);



		uint8_t typ_1 = typ&0x02;
		typ_1 = typ_1 >> 1;

		uint8_t typ_0 = typ&0x01;

		tx_buf[0] =  0x3A;
		tx_buf[1] =  typ_1;
		spi_engine_write_and_read(spi_eng_com,(uint8_t *)&tx_buf, 2);

		tx_buf[0] =  0x3C;
		tx_buf[1] =  typ_0;
		spi_engine_write_and_read(spi_eng_com,(uint8_t *)&tx_buf, 2);


 }







void ppos_set_tangenta(ena_type ena)
{
    ppos.creg2.inside.Tang = ena;
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);
}












 /*

 void spi_engine_npu_com_board_set_sd_on(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x04;
		tx_buf[1] =  0x00;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
}

 void spi_engine_npu_com_board_set_sd_off(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x04;
		tx_buf[1] =  0x01;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
}

 void spi_engine_npu_com_board_set_sd_status(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x84;
		tx_buf[1] =  0x00;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

		//tx_buf[0] =  0x00;
		//tx_buf[1] =  0x00;
		//spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

		xil_printf("com_board_set_sd_status = %02X \n\r ", tx_buf[1]);
}




 void spi_engine_npu_pow_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{

	uint8_t tx_buf[2];
	tx_buf[0] = 0xA4;
	tx_buf[1] = 0x00;
	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

	status_npu->cntrl_temp_filt_board = 0x01&tx_buf[1];

}



 void spi_engine_npu_pow_board_set_ena(struct no_os_spi_desc*  spi_eng, pow_cntrl_type cntrl_type,  ena_type ena )
{

   uint8_t tx_buf[2];
   if ( cntrl_type == UF_DIAP_1 ) // Включение частотного диапазона №1 ячейки фильтрации
   {
		tx_buf[0] = 0x25;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( cntrl_type == UF_PIT )    // Питание ячейки фильтрации
   {
		tx_buf[0] = 0x26;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( cntrl_type ==  UF_DIAP_2 ) // Включение частотного диапазона №2 ячейки фильтрации
   {
		tx_buf[0] = 0x27;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

}


*/


 void modem_init()
 {




     #ifdef IS_ANTSDR

	 ppos.creg2.inside.Rx1B = 0;
	 ppos.creg2.inside.Tx1B = 0;
	 ppos.creg2.inside.Rx2B = 0;
	 ppos.creg2.inside.Tx2B = 0;

	 #endif

	 ppos.creg2.word = 0;



     #ifdef  IS_BORT

	 ppos.creg2.inside.Pit = 1;
	 ppos.creg2.inside.Fdiap = 0;
	 ppos.creg2.inside.GPIOnR_TYPE = 2;

     #else
	  ppos.creg2.inside.GPIOnR_TYPE = 1;
     #endif

	 ppos.creg2.inside.Tang = 0;
	 ppos.creg2.inside.Tang_avt = 0;
	 //ppos.creg2.inside.GPIOnR_TYPE = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);


     ppos.creg3.word = 0;
     ppos.creg3.inside.AD9361_ENABLE =1;
	 ppos.creg3.inside.AD9361_TXnRX = 1;
     #ifdef IS_MASTER
	  ppos.creg3.inside.ENABLE_AUTO = 1;
     #endif
     #ifdef IS_SLAVE
      ppos.creg3.inside.ENABLE_AUTO = 0;
     #endif
	 ppos.creg3.inside.TXnRX_AUTO = 1;
     ppos.creg3.inside.MasterNSlave = 0;
     ppos.creg3.inside.IS_TDMA = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 12, ppos.creg3.word);


	 ppos.creg4.inside.txnrx_setup = 4500;
	 ppos.creg4.inside.txnrx_hold =  4500;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 16, ppos.creg4.word);


	 ppos.creg5.inside.slot_dur = 4000;//1000;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 20, ppos.creg5.word);


     ppos.status_on = 1;
     IP4_ADDR(&ppos.status_ip, 192, 168, 250, 22);
     ppos.status_port = 5007;






	 tx_chan0.creg0.inside.bod = 2;
	 tx_chan0.creg0.inside.code = 0;
	 tx_chan0.creg0.inside.mod = 2;
	 tx_chan0.creg0.inside.mux_ad9361_din = 5;
	 tx_chan0.creg0.inside.pkt_len = 4800;//576;
	 tx_chan0.creg0.inside.scr_on = 1;
	 tx_chan0.creg0.inside.gain = 0;
	 tx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, tx_chan0.creg0.word);


  	 tx_chan0.creg2.inside.ad9361_tx_freq = 3500000;//kHz
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 2*4,  (u32) tx_chan0.creg2.word);

  	 uint64_t freq =  tx_chan0.creg2.word;
  	 freq = freq*1000;
  	 ad9361_set_tx_lo_freq (ad9361_phy, freq);


  	 tx_chan0.creg3.inside.ad9361_tx_atten = 0x000003ff&(40000/250); //mdB/250  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   Tx2
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 3*4,  (u32) tx_chan0.creg3.word);

     uint32_t atten =  tx_chan0.creg3.inside.ad9361_tx_atten;
     atten*=250;

  	 ad9361_set_tx_attenuation (ad9361_phy, 1, atten);



	 rx_chan0.creg0.inside.bod = 2;
	 rx_chan0.creg0.inside.code = 0;
	 rx_chan0.creg0.inside.mod = 2;
	 rx_chan0.creg0.inside.pkt_len = 4800;//576;
	 rx_chan0.creg0.inside.scr_on = 1;
	 rx_chan0.creg0.inside.rcos_filt_on = 1;
	 rx_chan0.creg0.inside.rot_on = 1;
	 rx_chan0.creg0.inside.freq_shift_on = 0;

	 rx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0, rx_chan0.creg0.word);


 	 rx_chan0.creg1.inside.ad9361_rx_freq = 3500000;
     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 1*4,  (u32) rx_chan0.creg1.word);

     freq =  rx_chan0.creg1.word;
     freq = freq*1000;
     ad9361_set_rx_lo_freq (ad9361_phy, freq);




#ifdef IS_ZED_BOARD

  //d[0]=0x80; d[1]=0x0a;d[2]=0x12;
  //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

	 ad9361_set_rx_lo_freq (ad9361_phy,3500040000);
	 ad9361_set_tx_lo_freq (ad9361_phy,3500040000);

#endif

 }






