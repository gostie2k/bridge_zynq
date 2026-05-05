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




/* ========================================================================
 *   MAX7301 — инициализация с верификацией каждой записи
 * ======================================================================== */

/* Если хочешь убрать всю отладку - поставь 0 */
#define MAX7301_VERIFY_INIT  0

/**
 * Чтение регистра MAX7301 (две SPI-транзакции).
 */
static uint8_t max7301_read_reg(struct no_os_spi_desc *spi, uint8_t addr)
{
    uint8_t buf[2];

    buf[0] = 0x80 | (addr & 0x7F);
    buf[1] = 0x00;
    spi_engine_write_and_read(spi, buf, 2);

    buf[0] = 0x00;
    buf[1] = 0x00;
    spi_engine_write_and_read(spi, buf, 2);

    return buf[1];
}

/**
 * Простая запись регистра (без верификации).
 */
static void max7301_write_reg(struct no_os_spi_desc *spi,
                              uint8_t addr, uint8_t data)
{
    uint8_t buf[2] = { (uint8_t)(addr & 0x7F), data };
    spi_engine_write_and_read(spi, buf, 2);
}

/**
 * Запись с верификацией.
 *   verify_strict = 1: сравниваем readback с записанным, при несовпадении - FAIL.
 *                       Используется для config-регистров (0x04, 0x09..0x0F)
 *                       и для записей в порты, у которых ВСЕ биты являются выходами.
 *   verify_strict = 0: запись и чтение, печать readback БЕЗ проверки.
 *                       Используется для группы 0x40..0x5F, где часть бит -
 *                       выходы, часть - физуровень входов; сравнивать побайтно нельзя.
 *
 * Возвращает 0 на успех, -1 на FAIL.
 */
static int max7301_wr_v(struct no_os_spi_desc *spi,
                        uint8_t addr, uint8_t data,
                        uint8_t verify_strict,
                        const char *what)
{
    max7301_write_reg(spi, addr, data);

#if MAX7301_VERIFY_INIT
    uint8_t rb = max7301_read_reg(spi, addr);

    if (verify_strict) {
        if (rb == data) {
            xil_printf("  [OK]   0x%02X <- 0x%02X            : %s\r\n",
                       addr, data, what);
            return 0;
        } else {
            xil_printf("  [FAIL] 0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
                       addr, data, rb, what);
            return -1;
        }
    } else {
        xil_printf("  [WR]   0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
                   addr, data, rb, what);
        return 0;
    }
#else
    (void)addr; (void)data; (void)verify_strict; (void)what;
    return 0;
#endif
}

/* ========================================================================
 *   COM-board (CS2) — ячейка коммутационная
 *   Возвращает число ошибок верификации (0 - всё хорошо).
 * ======================================================================== */
void spi_engine_npu_com_board_init(struct no_os_spi_desc **spi_eng)
{
    int errs = 0;

    /* --- SPI Engine --- */
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr = SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type                = SPI_ENGINE;
    spi_engine_init_par.data_width          = 8;
    spi_engine_init_par.cs_delay            = 0;
    spi_engine_init_par.ref_clk_hz          = 100000000;
    spi_engine_init_par.sdo_idle_state      = 1;

    struct no_os_spi_init_param no_os_spi_init = {0};
    no_os_spi_init.extra        = &spi_engine_init_par;
    no_os_spi_init.mode         = NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id    = 1;
    no_os_spi_init.chip_select  = NPU_COM_BOARD_CS;
    no_os_spi_init.max_speed_hz = 25000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    xil_printf(">>> COM init: device_id=%d, chip_select=%d\r\n",
                       (*spi_eng)->device_id, (*spi_eng)->chip_select);
    spi_engine_set_speed(*spi_eng, 1000000);

    xil_printf("\r\n=== MAX7301 init: COM-board (CS2) ===\r\n");

    /* --- Конфиг режимов портов: верификация строгая --- */
    errs += (max7301_wr_v(*spi_eng, 0x09, 0xFF, 1, "P[7:4]   In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0A, 0xFF, 1, "P[11:8]  In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0B, 0xFD, 1, "P[15:13] In+PU, P12 Out") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0C, 0xFF, 1, "P[19:16] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0D, 0x57, 1, "P[23:21] Out, P20 In+PU") != 0);
    //errs += (max7301_wr_v(*spi_eng, 0x0E, 0xF5, 1, "P[27:26] In+PU, P[25:24] Out") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0E, 0xD5, 1, "P[27:26] In+PU, P[25:24] Out") != 0);
    //errs += (max7301_wr_v(*spi_eng, 0x0F, 0xFF, 1, "P[31:28] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0F, 0x5D, 1, "P[31:28] In+PU") != 0);

    /* --- Начальные значения выходов ---
     * 0x2C - одиночный регистр P12, всё что писали - читается обратно (strict).
     * 0x55 - групповая запись P[28:21]; внутри есть и выходы (P21..P25)
     *        и входы (P26..P28) - НЕ strict.
     */
    errs += (max7301_wr_v(*spi_eng, 0x2C, 0x00, 1, "P12        init=0") != 0);
    max7301_wr_v(*spi_eng, 0x55, 0x00, 0, "P[28:21]   init=0 (group)");

    /* --- Снимаем shutdown в самом конце --- */
    errs += (max7301_wr_v(*spi_eng, 0x04, 0x01, 1, "Shutdown OFF (Normal)") != 0);

#if MAX7301_VERIFY_INIT
    if (errs == 0)
        xil_printf("=== COM-board: OK (no verify errors) ===\r\n\r\n");
    else
        xil_printf("=== COM-board: %d VERIFY ERRORS ===\r\n\r\n", errs);
#endif

    return errs;
}

/* ========================================================================
 *   POW-board (CS1) — ячейка распределения питания
 * ======================================================================== */
void spi_engine_npu_pow_board_init(struct no_os_spi_desc** spi_eng)
{
    int errs = 0;

    /* --- SPI Engine --- */
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr = SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type                = SPI_ENGINE;
    spi_engine_init_par.data_width          = 8;
    spi_engine_init_par.cs_delay            = 0;
    spi_engine_init_par.ref_clk_hz          = 100000000;
    spi_engine_init_par.sdo_idle_state      = 0;

    struct no_os_spi_init_param no_os_spi_init = {0};
    no_os_spi_init.extra        = &spi_engine_init_par;
    no_os_spi_init.mode         = NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id    = 1;
    no_os_spi_init.chip_select  = NPU_POW_BOARD_CS;
    no_os_spi_init.max_speed_hz = 25000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    spi_engine_set_speed(*spi_eng, 1000000);
    xil_printf(">>> POW init: device_id=%d, chip_select=%d\r\n",
                   (*spi_eng)->device_id, (*spi_eng)->chip_select);
    xil_printf("\r\n=== MAX7301 init: POW-board (CS1) ===\r\n");

    /* --- Конфиг режимов портов --- */
    errs += (max7301_wr_v(*spi_eng, 0x09, 0xF5, 1, "P[7:6]   In+PU, P[5:4] Out") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0A, 0xFF, 1, "P[11:8]  In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0B, 0xFF, 1, "P[15:12] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0C, 0xFF, 1, "P[19:16] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0D, 0xFF, 1, "P[23:20] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0E, 0xFF, 1, "P[27:24] In+PU") != 0);
    errs += (max7301_wr_v(*spi_eng, 0x0F, 0xFF, 1, "P[31:28] In+PU") != 0);

    /* --- Начальные значения выходов ---
     * 0x40 - групповой регистр P[7:4]; P4,P5 - выходы, P6,P7 - входы.
     * Читать обратно строго побайтно нельзя - non-strict.
     */
    max7301_wr_v(*spi_eng, 0x40, 0x03, 0, "P[7:4]     init=0x03 (group)");

    /* --- Снимаем shutdown в самом конце --- */
    errs += (max7301_wr_v(*spi_eng, 0x04, 0x01, 1, "Shutdown OFF (Normal)") != 0);

#if MAX7301_VERIFY_INIT
    if (errs == 0)
        xil_printf("=== POW-board: OK (no verify errors) ===\r\n\r\n");
    else
        xil_printf("=== POW-board: %d VERIFY ERRORS ===\r\n\r\n", errs);
#endif

    return errs;
}



 void spi_engine_npu_com_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{

	uint8_t tx_buf[2];
	tx_buf[0] = 0xC4;
	tx_buf[1] = 0x00;
	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

	status_npu->tx_panel_1.word = tx_buf[1]; // !!!!!!!!!!!!!!! .. 7:0 -> 0:7

	tx_buf[0] = 0xCD;
	tx_buf[1] = 0x00;
	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

	status_npu->tx_panel_2.word = tx_buf[1]; // !!!!!!!!!!!!!!!!!! .. 7:0 -> 0:7

}


 void spi_engine_npu_com_board_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{

   uint8_t tx_buf[2];
   if ( com_dev ==TX_PANEL_1 ) //    1
   {
		tx_buf[0] = 0x2C;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==TX_PANEL_2 ) //    2
   {
		tx_buf[0] = 0x35;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_1 ) //    1
   {
		tx_buf[0] = 0x36;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_2 ) //    2
   {
		tx_buf[0] = 0x37;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_3 ) //    3
   {
		tx_buf[0] = 0x38;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_4 ) //    4
   {
		tx_buf[0] = 0x39;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

}



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







void ppos_set_tangenta(struct no_os_spi_desc*  spi_eng, ena_type ena)
{
    ppos.creg2.inside.Tang = ena;
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);




//	uint8_t typ_1 = 0x01&ena;
//	uint8_t typ_0 = 0x01&(~ena);
//
//	uint8_t tx_buf[2];
//
//	tx_buf[0] =  0x3E;
//	tx_buf[1] =  typ_1;
//	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
//
//	tx_buf[0] =  0x3F;
//	tx_buf[1] =  typ_0;
//	spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);


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
   if ( cntrl_type == UF_DIAP_1 ) //    1
   {
		tx_buf[0] = 0x25;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( cntrl_type == UF_PIT )    //
   {
		tx_buf[0] = 0x26;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( cntrl_type ==  UF_DIAP_2 ) //    2
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


     ppos.creg2.inside.Tang = 0;
	 ppos.creg2.inside.Tang_avt = 0;
	 ppos.creg2.inside.GPIOnR_TYPE = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);



	 ppos.creg4.inside.txnrx_setup = 2000;
	 ppos.creg4.inside.txnrx_hold =  4500;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 16, ppos.creg4.word);

     ppos.status_on = 0;
     ppos.status_ip[0] = 192;
     ppos.status_ip[1] = 168;
     ppos.status_ip[2] = 250;
     ppos.status_ip[3] = 22;

     ppos.status_port = 5007;













	 tx_chan0.creg0.inside.bod = 2;
	 tx_chan0.creg0.inside.code = 0;
	 tx_chan0.creg0.inside.mod = 2;
	 tx_chan0.creg0.inside.mux_ad9361_din = 5;
	 tx_chan0.creg0.inside.pkt_len = 576;
	 tx_chan0.creg0.inside.scr_on = 1;
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
	 rx_chan0.creg0.inside.pkt_len = 576;
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



/*

 void send_modem_status(struct stat_modem* status) {


 	udp_bind_netif (debug_pcb, &bridge_netif);

     struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct stat_modem), PBUF_RAM);
     if (!p) {
         xil_printf("Failed to allocate pbuf for deb packet\n\r");
         return;
     }

     memcpy((uint8_t*) p->payload, (uint8_t*) status, sizeof(struct stat_modem));

     err_t out = udp_sendto(debug_pcb, p, &RemoteAddr, RemotePortStatus);

     if (out != ERR_OK) {
         xil_printf("Error udp_sendto debug_pcb: %d\n\r", out);
     }

     pbuf_free(p);
 }

*/


