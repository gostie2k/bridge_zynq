/*
 * Copyright (c) 2017 KNK.
 * All rights reserved.


 * This file is part of the PKRV MODEM Project.
 *
 * Author: KNK <@.>
 *
 */

#ifndef MODEM_H_
#define MODEM_H_

/* ===================================================================
 *  CREG memory map (contract with PL)
 *
 *  AXI address of creg N in a block:
 *      AXI_CNTRL_REG_BASEADDR + block_offset + N*4
 *
 *  Three blocks share AXI_CNTRL_REG_BASEADDR. The 0x20/0x30 layout
 *  reserves 8 cregs per block in PL even though we use fewer.
 * =================================================================== */
#define BASE_PAR_OFFSET       0x00   /* ppos.cregN     — global       */
#define TX_CHAN0_PAR_OFFSET   0x20   /* tx_chan0.cregN — TX channel   */
#define RX_CHAN0_PAR_OFFSET   0x30   /* rx_chan0.cregN — RX channel   */

#define BASE_PAR_NUM_CREGS    6
#define TX_CHAN0_NUM_CREGS    4
#define RX_CHAN0_NUM_CREGS    2

/* Group IDs as used by the 0xA0 protocol. */
typedef enum {
    CREG_GROUP_BASE     = 0,  /* not exposed via 0xA0 currently */
    CREG_GROUP_TX_CHAN0 = 1,
    CREG_GROUP_RX_CHAN0 = 2,
    CREG_GROUP_COUNT
} creg_group_t;

#ifndef IS_QT

#include "ad9361/no_os_spi.h"
#include "spi_engine/spi_engine.h"
#include "platform_config.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"
#include "ad9361/ad9361_api.h"
#include "sleep.h"

#endif

#include <stdio.h>
#include <stdint.h>


typedef enum {
    DISABLED = 0,
    ENABLED  = 1
} ena_type;


typedef enum {
    TX_PANEL_1 = 0,
    TX_PANEL_2 = 1,
	RX_PANEL_1 = 2,
	RX_PANEL_2 = 3,
	RX_PANEL_3 = 4,
	RX_PANEL_4 = 5
} com_dev_type;

typedef enum {
	F_4000_4200_MHz = 0,
	F_3700_3980_MHz = 1,
	F_3400_3600_MHz = 2,
	BYPASS=3
} pow_cntrl_type;



typedef enum {
    CMD_SRC_UART,
    CMD_SRC_UDP
} cmd_source_t;




#pragma pack(push,1)
struct stat_cntrl_tx
{
	uint16_t cnt_pkt;
	uint16_t cnt_err_pkt;
	uint16_t last_pkt;
	uint32_t last_dat;


	float max_pow_cntrl;
	float min_pow_cntrl;
	float acc_pow_cntrl;

};
#pragma pack(pop)



#pragma pack(push,1)
struct stat_rx
{
	uint16_t cnt_pkt;
	uint16_t cnt_err_pkt;
	uint16_t last_pkt;

	float max_pow;
	float min_pow;
	float acc_pow;

};
#pragma pack(pop)



#pragma pack(push,1)
struct stat_npu
{

	   union
	   {
		  uint8_t word;
		  struct
		   {
		     //from bit#0....down to bit#7
			  uint8_t cntrl_temp1:1;
			  uint8_t cntrl_temp2:1;
			  uint8_t cntrl_temp3:1;
			  uint8_t cntrl_temp4:1;
			  uint8_t cntrl_pow1:1;
			  uint8_t cntrl_pow2:1;
			  uint8_t cntrl_pow3:1;
			  uint8_t cntrl_pow4:1;
		    } inside;
	   } tx_panel_1;

	   union
	   {
		  uint8_t word;
		  struct
		   {
		     //from bit#0....down to bit#7
			  uint8_t cntrl_temp1:1;
			  uint8_t cntrl_temp2:1;
			  uint8_t cntrl_temp3:1;
			  uint8_t cntrl_temp4:1;
			  uint8_t cntrl_pow1:1;
			  uint8_t cntrl_pow2:1;
			  uint8_t cntrl_pow3:1;
			  uint8_t cntrl_pow4:1;
		    } inside;
	   } tx_panel_2;

	   uint8_t cntrl_temp_filt_board;

};
#pragma pack(pop)



#pragma pack(push,1)
struct stat_modem
{
	float TempFPGA;
	float TempAD9361;
	float pps_pow_cntrl;
	float pps_pow;
	uint8_t gpio_status;
	struct stat_cntrl_tx status_cntrl_tx;
	struct stat_rx  status_rx;
    struct stat_npu status_npu;
};
#pragma pack(pop)


/* ========== lwIP stats response (from user's version) ========== */

#define CMD_PROTO_MAGIC 0x90
#define CMD_GET_STATS   0x20

#pragma pack(push, 1)
typedef struct {
    uint8_t head[2];      // 0: 0x90, 1: 0x20

    // HEAP (mem.c)
    uint32_t mem_used;
    uint32_t mem_max;
    uint32_t mem_err;

    // PBUF Headers (memp.c)
    uint32_t pbuf_used;
    uint32_t pbuf_max;
    uint32_t pbuf_err;

    // PBUF POOL (payloads)
    uint32_t pool_used;
    uint32_t pool_max;
    uint32_t pool_err;

    // Link drops
    uint32_t link_drop;
} stats_resp_t;
#pragma pack(pop)


struct stat_modem* stat_modem_init();
void modem_fill_stats(stats_resp_t *resp);
void modem_print_stats(void);

void send_modem_status(struct stat_modem* status);


/* ========== modem control structures (from video_debug version) ========== */

/**
 * @struct ppos_rx
 * @brief Structure parameters for PPOS
 */
#pragma pack(push,1)
struct ppos_rx
{

	/** set rx parameters */
   union
   {
	  uint32_t word;
	  struct
	   {
	     //from bit#0....down to bit#31
          uint16_t mod:2;
          uint16_t bod:2;
          uint16_t code:3;
          uint16_t scr_on:1;
          uint16_t rot_on:1;
          uint16_t rcos_filt_on:1;
          uint16_t freq_shift_on:1;
          uint16_t run:1;
          uint16_t reserve: 4;
          uint16_t pkt_len;
	    } inside;
	} creg0;

	/** set rx frequency AD9361 */
   union
    {
	  uint32_t word;
	  struct
	   {
		  uint32_t ad9361_rx_freq;
	    } inside;
	} creg1;



};
#pragma pack(pop)





/**
 * @struct ppos_tx
 * @brief Structure parameters for PPOS
 */
#pragma pack(push,1)
struct ppos_tx
{

	/** create impulse, after sset sclr on */
   union
   {
	  uint32_t word;
	  struct
	   {
	     //from bit#0....down to bit#31
		  uint16_t mod:2;
		  uint16_t bod:2;
		  uint16_t code:3;
		  uint16_t scr_on: 1;
		  uint16_t mux_ad9361_din:4;
          uint16_t run: 1;
          uint16_t reserve:3;
	      uint16_t pkt_len;
	    } inside;
	} creg0;


	/** set test generator frequency */
   union
    {
	  uint32_t word;
	  struct
	   {
		  uint32_t test_gen_fc;
	    } inside;
	} creg1;

	/** set tx frequency AD9361 */
   union
    {
	  uint32_t word;
	  struct
	   {
		  uint32_t ad9361_tx_freq;
	    } inside;
	} creg2;

	/** set tx frequency AD9361 */
   union
    {
	  uint32_t word;
	  struct
	   {
		  uint16_t ad9361_tx_atten:10;
		  uint16_t reserve_0:6;
		  uint16_t reserve_1;
	    } inside;
	} creg3;




};
#pragma pack(pop)







/**
 * @struct ppos_par
 * @brief Structure parameters for PPOS
 */
#pragma pack(push,1)
struct ppos_par
{

	/** version */
   union
   {
	  uint32_t word;
	  struct
	   {
		     //from bit#0....down to bit#31
			     /** version */
		         unsigned sn:12;
		         /** product version */
			     unsigned pv:8;
			     /** product number */
			     unsigned pn:12;
	   } inside;
   } creg0;


	/** create impulse, after sset sclr on */
   union
   {
	  uint32_t word;
	  struct
	   {
	     //from bit#0....down to bit#31
	     unsigned imit_ps_int_gpio_63:1;
	     unsigned imit_ps_int_gpio_62:1;
	     unsigned mfunc_start:1;
	     unsigned test_gen_set_fc:1;
	     unsigned set_dac_ltc2630:1;
	     unsigned reserve:27;
	    } inside;
	} creg1;


	/** hardware settings */
   union
    {
	  uint32_t word;
	  struct
	   {
	     //from bit#0....down to bit#31
	     unsigned Rx1B:1; // !Rx1A
	     unsigned Tx1B:1; // !Tx1A
	     unsigned Rx2B:1; // !Rx2A
	     unsigned Tx2B:1; // !Tx2A
	     unsigned GPIO1R:1;
	     unsigned GPIO2R:1;
	     unsigned GPIO3R:1;
	     unsigned GPIO4R:1;
	     unsigned GPIO5R:1;
	     unsigned GPIO6R:1;
	     unsigned GPIO1R_DIR:1;
		 unsigned GPIO2R_DIR:1;
		 unsigned GPIO3R_DIR:1;
		 unsigned GPIO4R_DIR:1;
		 unsigned GPIO5R_DIR:1;
		 unsigned GPIO6R_DIR:1;
	     unsigned GPIOnR_TYPE:2;
	     unsigned Tang:1;
	     unsigned Tang_avt:1;
	     unsigned Pit:1;
	     unsigned Fdiap:3; //air: 0 - 3.4-3.6, !0 - 3.7-3.9

	     unsigned GPIO1R_IN:1;
		 unsigned GPIO2R_IN:1;
		 unsigned GPIO3R_IN:1;
		 unsigned GPIO4R_IN:1;
		 unsigned GPIO5R_IN:1;
		 unsigned GPIO6R_IN:1;
		 unsigned reserve:2;

	    } inside;
	} creg2;



	/** set line parameters for gain board slave modem */
   union
    {
	  uint32_t word;
	  struct
	   {
		     //from bit#0....down to bit#31
		     unsigned nuf_pit:1;
		     unsigned uf_tangenta:1;
		     unsigned uf_sel:1;
		     unsigned uf_tangenta_ena_avt:1;
		     unsigned reserve:28;
	    } inside;
	} creg3;




	/** time selection parameters */
   union
    {
	  uint32_t word;
	  struct
	   {
		  uint16_t txnrx_setup;
		  uint16_t txnrx_hold;
	    } inside;
	} creg4;



	/** set cmd ltc2630 */
   union
    {
	  uint32_t word;
	  struct
	   {
		     //from bit#0....down to bit#31
		     unsigned ltc2630_dac_data:8;
		     unsigned ltc2630_dac_com:4;
		     unsigned reserve:20;
	    } inside;
	} creg5;





	struct ppos_tx* ppos_txs;
	struct ppos_rx* ppos_rxs;

	uint8_t   num_ppos_txs;
	uint8_t   num_ppos_rxs;

	uint8_t   status_on;
	uint8_t   status_ip[4];

	uint16_t  status_port;



};
#pragma pack(pop)



void spi_engine_ad9361_init(struct no_os_spi_desc**  spi_eng);
void spi_engine_npu_com_board_init(struct no_os_spi_desc**  spi_eng);
void spi_engine_npu_pow_board_init(struct no_os_spi_desc**  spi_eng);

void spi_engine_npu_com_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu);
void spi_engine_npu_com_board_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena);
void spi_engine_npu_pow_board_set_f_diap(struct no_os_spi_desc*  spi_eng_com, struct no_os_spi_desc*  spi_eng,  pow_cntrl_type typ );

void ppos_set_tangenta(struct no_os_spi_desc*  spi_eng, ena_type ena);


/* ===================================================================
 *  CREG access API
 *
 *  creg_sync   — push current RAM mirror of (group, idx) into PL.
 *  creg_apply  — set (group, idx) to `word`: patches RAM mirror,
 *                applies pre-write fixups (run=0 on creg0, atten mask),
 *                writes to PL, dispatches AD9361 side-effects.
 * =================================================================== */
void creg_sync(creg_group_t group, uint8_t idx);
void creg_apply(creg_group_t group, uint8_t idx, uint32_t word);


void modem_init();
#endif // MODEM_H_
