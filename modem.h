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

//CREG OFFSET
#define BASE_PAR_OFFSET 0
#define TX_CHAN0_PAR_OFFSET 4*8
#define RX_CHAN0_PAR_OFFSET 4*(8+4)
#define PELENG_PAR_OFFSET 0

#ifndef IS_QT

#include "ad9361/no_os_spi.h"
#include "spi_engine/spi_engine.h"
#include "platform_config.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"
#include "ad9361/ad9361_api.h"
#include "sleep.h"
#include "lwip/ip4_addr.h"

#endif

#ifdef IS_QT

struct ip4_addr {
  uint32_t addr;
};

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











#define BUF_DEB_CHAN_RX_DEPTH 128


#pragma pack(push,1)
struct deb_chan_rx
{
	uint8_t    cnan_id;
	uint8_t    cnt_seg;
	uint8_t    ad9361_gain;
	uint32_t   mer;
	//uint32_t  freq;
	//uint16_t  agc_gain;
	uint32_t   del_ack;
	//uint8_t   id_pofile;
	uint8_t    ad9361_rssi;
	union
	   {
		  uint32_t word;
		  struct
		   {
		     //from bit#0....down to bit#7
			  uint32_t rs_dec_bit_err_10:8;
			  uint32_t rs_dec_bit_err_01:8;
			  uint32_t rs_dec_err_cnt:5;
			  uint32_t rs_dec_err_found:1;
			  uint32_t rs_dec_fail:1;
			  uint32_t reserve:9;
		    } inside;
	   }      rs_dec_stat;

	 uint16_t vit_err_cnt;

};
#pragma pack(pop)






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

	uint8_t max_gain_index;
	uint8_t min_gain_index;
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
	   uint32_t del_ack;

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
    //uint32_t creg2;
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
          uint16_t deb_append_ena:1;
          uint16_t reserve:3;
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
          uint16_t gain:3;
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
		         uint32_t sn:12;
		         /** product version */
		         uint32_t pv:8;
			     /** product number */
		         uint32_t pn:12;
	   } inside;
   } creg0;


	/** create impulse, after sset sclr on */
   union
   {
	  uint32_t word;
	  struct
	   {
		  uint32_t ad9361_spi_typ:1; // 1- spi_engine, 0 - ~spi_engine
	     //from bit#0....down to bit#31
		 //uint32_t imit_ps_int_gpio_63:1;
		 //uint32_t imit_ps_int_gpio_62:1;
		 //uint32_t mfunc_start:1;
		 //uint32_t test_gen_set_fc:1;
		 //uint32_t set_dac_ltc2630:1;
		 uint32_t reserve:31;
	    } inside;
	} creg1;


	/** hardware settings */
   union
    {
	  uint32_t word;
	  struct
	   {
	     //from bit#0....down to bit#31
		  uint32_t Rx1B:1; // !Rx1A
		  uint32_t Tx1B:1; // !Tx1A
		  uint32_t Rx2B:1; // !Rx2A
		  uint32_t Tx2B:1; // !Tx2A
		  uint32_t GPIO1R:1;
		  uint32_t GPIO2R:1;
		  uint32_t GPIO3R:1;
		  uint32_t GPIO4R:1;
		  uint32_t GPIO5R:1;
		  uint32_t GPIO6R:1;
		  uint32_t GPIO1R_DIR:1;
		  uint32_t GPIO2R_DIR:1;
		  uint32_t GPIO3R_DIR:1;
		  uint32_t GPIO4R_DIR:1;
		  uint32_t GPIO5R_DIR:1;
		  uint32_t GPIO6R_DIR:1;
		  uint32_t GPIOnR_TYPE:2;
		  uint32_t Tang:1;
		  uint32_t Tang_avt:1;
		  uint32_t Pit:1;
		  uint32_t Fdiap:3; //air: 0 - 3.4-3.6, !0 - 3.7-3.9

		  uint32_t GPIO1R_IN:1;
		  uint32_t GPIO2R_IN:1;
		  uint32_t GPIO3R_IN:1;
		  uint32_t GPIO4R_IN:1;
		  uint32_t GPIO5R_IN:1;
		  uint32_t GPIO6R_IN:1;
		  uint32_t reserve:2;

	    } inside;
	} creg2;



	/** set line parameters for gain board slave modem */
   union
    {
	  uint32_t word;
	  struct
	   {
		     //from bit#0....down to bit#31
		  uint32_t MasterNSlave:1;
		  uint32_t TXnRX_AUTO:1;
		  uint32_t ENABLE_AUTO:1;
		  uint32_t AD9361_TXnRX:1;
		  uint32_t AD9361_ENABLE:1;
		  uint32_t IS_TDMA:1;
		  uint32_t reserve:26;
	    } inside;
	} creg3;




	/** time selection parameters */

	/*

   union
    {
	  uint32_t word;
	  struct
	   {
		  uint16_t txnrx_setup;
		  uint16_t txnrx_hold;
	    } inside;
	} creg4;



	  union
	    {
		  uint32_t word;
		  struct
		   {
			  uint16_t slot_dur; //us
			  uint16_t num_seg:6;
			  uint16_t reserve:10;
		    } inside;
		} creg5;


*/



	 union
	    {
		  uint32_t word;
		  struct
		   {
			  uint8_t txnrx_setup;
			  uint8_t txnrx_hold;
			  uint8_t vco_setup;
			  uint8_t num_seg:4;
			  uint8_t reserve:4;
		    } inside;
		} creg4;




		  union
		    {
			  uint32_t word;
			  struct
			   {
				  uint16_t slot_dur; //us
				  uint16_t reserve:16;
			    } inside;
			} creg5;









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
	} creg6;



		   union
		    {
			  uint32_t word;
			  struct
			   {
				     //from bit#0....down to bit#31
				  uint32_t del_ack;
			    } inside;
			} creg7;









	struct ppos_tx* ppos_txs;
	struct ppos_rx* ppos_rxs;

	uint8_t   num_ppos_txs;
	uint8_t   num_ppos_rxs;

	uint8_t   status_on;
	struct ip4_addr   status_ip;

	uint16_t  status_port;



};
#pragma pack(pop)



void spi_engine_ad9361_init(struct no_os_spi_desc**  spi_eng);
void spi_engine_npu_com_board_init(struct no_os_spi_desc**  spi_eng);
void spi_engine_npu_pow_board_init(struct no_os_spi_desc**  spi_eng);

void spi_engine_npu_com_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu);
void spi_engine_npu_com_board_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena);
//void spi_engine_npu_pow_board_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu);
//void spi_engine_npu_pow_board_set_ena(struct no_os_spi_desc*  spi_eng, pow_cntrl_type cntrl_type,  ena_type ena );
//void spi_engine_npu_pow_board_set_f_diap(struct no_os_spi_desc*  spi_eng,  pow_cntrl_type typ );
void spi_engine_npu_pow_board_set_f_diap(struct no_os_spi_desc*  spi_eng_com, struct no_os_spi_desc*  spi_eng,  pow_cntrl_type typ );
void ppos_set_tangenta(ena_type ena);




void modem_init();

/**
 * @struct peleng
 * @brief Параметры импульсного сигнала (пеленг). Пишутся по AXI в
 *        AXI_CNTRL_REG1_BASEADDR + PELENG_PAR_OFFSET.
 *        Управление: UDP 0xA0 group=4.
 */
#pragma pack(push, 1)
struct peleng {

    /** set parameters of the impulse signal */
    union {
        uint32_t word;
        struct {
            // from bit#0....down to bit#31
            uint32_t dut_imp : 12; // fs = 120MHz tacts
            uint32_t per_imp : 16; // fs = 120MHz tacts
            uint32_t run : 1;
            uint32_t barker : 3;
        } inside;
    } creg0;

    /** set parameters of the impulse signal */
    union {
        uint32_t word;
        struct {
            uint32_t num_imp : 10;   // in group
            uint32_t num_group : 10; // if (0) - infinity
            uint32_t per_group : 12; // ms
        } inside;
    } creg1;

    /** set freq shift of the impulse signal */
    union {
        uint32_t word;
        struct {
            uint32_t freq_shift;
        } inside;
    } creg2;
};
#pragma pack(pop)

#endif // MODEM_H_
