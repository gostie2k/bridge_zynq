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






/* Статистика одного радиоканала за секунду. Одна и та же структура на оба
 * канала: информационный (параметры из rx_chan0) и управления (из tx_chan0).
 * Накапливается в send_deb_rx_append() по записям диагностики ПЛИС, уходит в
 * статусном пакете раз в секунду и после отправки сбрасывается — отправитель
 * ставит is_clr = 1, и следующая запись начинает счёт заново.
 *
 * Поля *_mean — сумма, а не среднее: делить на cnt_seg должен получатель. */
#pragma pack(push,1)
struct stat_rx_chan
{
	uint8_t  is_clr;
	uint8_t  pr_seg;        /* номер предыдущего сегмента, из пакета */
	uint8_t  npr_seg;
	uint16_t cnt_lost_seg;  /* разрывы счётчика сегментов            */
	uint16_t cnt_err_seg;   /* RS не восстановил                     */
	uint16_t cnt_seg;       /* принято успешно                       */
	uint16_t cnt_vit_bit_err;
	uint8_t  gain_min;
	uint8_t  gain_max;
	uint32_t gain_mean;
	float    rssi_min;
	float    rssi_max;
	float    rssi_mean;
	uint32_t mer_min;
	uint32_t mer_max;
	uint32_t mer_mean;
	uint32_t del_ack_mean;
	uint8_t  mod;
	uint8_t  code;
	float    pps_pow;       /* мощность, снятая по SPI раз в секунду */
	uint8_t  pps_gain;
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
/* Состояние выходов ячейки НПУ, ПРОЧИТАННОЕ с MAX7301, а не теневая
 * копия того, что мы ей посылали. Нужно именно это: команды 0x16/0x17
 * обратной связи не имеют, и до сих пор «ПРД1 ВКЛ» на экране означало
 * лишь то, что по кнопке щёлкнули.
 *
 * Читаются те же одиночные регистры портов (0x20 + номер), в которые
 * пишут set_ena и set_f_diap. Групповые регистры (0x40 + номер) дали бы
 * меньше кадров, но у них спорная адресация: протокол сопряжения
 * называет 0x40 группой «P4..P7», а по даташиту 0x40 — это восемь
 * портов начиная с P0, которых у AAX+ нет. Читать ровно тот регистр,
 * в который писали, — единственная проверяемая привязка. */
struct npu_outputs
{
	/* Бит на подрешётку, порядок com_dev_type:
	 * D0 ПРД1, D1 ПРД2, D2..D5 ПРМ1..ПРМ4. 1 — питание подано. */
	uint8_t ena;
	/* Код pow_cntrl_type, собранный из портов преселектора. */
	uint8_t f_diap;
	/* Регистр 0x04 как прочитан: D0 = 1 — ячейка вышла из shutdown.
	 * Заодно признак живости: единицу туда положила инициализация, и
	 * если она читается обратно, значит и SPI, и микросхема работают.
	 * Ноль или 0xFF — ячейка не отвечает, остальные поля смысла не
	 * имеют. */
	uint8_t cfg;
	uint8_t reserve;
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

	struct stat_rx_chan stat_rx;      /* информационный канал */
	struct stat_rx_chan stat_cntrl;   /* канал управления     */

	uint16_t slot_dur;                /* creg5, мкс           */
	uint8_t  num_seg;                 /* creg4: сегментов на передачу */
	uint8_t  num_rx_seg;              /* creg4: сегментов на приём    */

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
	     /* Rx1B/Tx1B/Rx2B/Tx2B были нужны только AntSDR и из ПЛИС убраны
	      * (knk_krt_bvp 791799c). Освободившиеся четыре бита забраны под
	      * расширение GPIOnR_TYPE, поэтому вся карта creg2 сдвинулась на
	      * 4 бита вниз. Прежние поля — в закомментированном creg4 ниже. */
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
		  /* Профиль распиновки шести линий AB_GPIO1R..6R внутри ppos_dsp:
		   * 0 — вручную из GPIOnR/GPIOnR_DIR, иначе зашитая в ПЛИС схема
		   * под конкретное изделие. Перечень значений — в modem_init(). */
		  uint32_t GPIOnR_TYPE:4;
		  uint32_t Tang:1;
		  uint32_t Tang_avt:1;
		  uint32_t Pit:1;
		  uint32_t Fdiap:3; //air: 0 - 3.4-3.6, !0 - 3.7-3.9
		  /* CONT_TANG — вторая тангента, отдельная линия на другую плату.
		   * Первая осталась за Tang, они независимы. 1 — передача, 0 —
		   * приём. _avt — линию ведёт автомат ПЛИС по тактам слота, сам
		   * бит NPU_CTX_nCRX тогда не смотрится. */
		  uint32_t NPU_CTX_nCRX:1;
		  uint32_t NPU_CTX_nCRX_avt:1;

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
		  uint32_t Rx1B:1; // !Rx1A  AntSDR
		  uint32_t Tx1B:1; // !Tx1A  AntSDR
		  uint32_t Rx2B:1; // !Rx2A  AntSDR
		  uint32_t Tx2B:1; // !Tx2A  AntSDR
		  uint32_t reserve:28;
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
			  uint8_t num_rx_seg:4;
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
/* Ячейка распределения питания и управления ЕРВА.468349.020: одна
 * MAX7301 на единственном chip select, см. шапку в modem.c */
void spi_engine_npu_init(struct no_os_spi_desc**  spi_eng);

void spi_engine_npu_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu);
void spi_engine_npu_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena);
void spi_engine_npu_set_f_diap(struct no_os_spi_desc*  spi_eng, pow_cntrl_type typ);
/* Чтение выходов ячейки. Кадров много (по два на регистр), поэтому
 * вызывается из главного цикла рядом с get_status, а не из обработчика
 * UDP: движок SPI один, и два обмена вперемешку испортили бы оба. */
void spi_engine_npu_get_outputs(struct no_os_spi_desc*  spi_eng, struct npu_outputs* out);

#if NPU_CELL_NEW && NPU_BRIDGE_DEBUG
/* Короткий опрос моста SC18IS604 по запросу — для проверки нового chip
 * select с экрана. Кадр тот же, что в начале bridge_debug(): команда
 * 0xFE («дай строку версии») не трогает шину I2C вообще, поэтому
 * проверяет ровно SPI до моста и ничего больше. */
struct bridge_probe {
    uint8_t  ver[8];    /* первые 8 байт ответа на 0xFE          */
    uint8_t  i2cclk;    /* регистр I2CClock, читается следом     */
    uint32_t cnt;       /* сколько опросов выполнено с загрузки  */
};
extern struct bridge_probe bridge_probe_last;
void spi_engine_bridge_probe(void);

/* Обход восьми INA226 за мостом. Заполняется в главном цикле по запросу
 * с экрана, отдаётся кэшем: полный обход — 24 транзакции I2C, около 7 мс,
 * делать его в обработчике UDP нельзя. */
struct npu_power {
    uint8_t  cnt;        /* счётчик обходов, для контроля свежести  */
    uint8_t  stat[8];    /* 0 — норма, иначе код I2CStat или 0xFE
                          * (ответил, но Die ID не 0x2260)          */
    uint16_t bus[8];     /* регистр 0x02, 1,25 мВ на разряд         */
    int16_t  shunt[8];   /* регистр 0x01, 2,5 мкВ на разряд         */
};
extern struct npu_power npu_pwr;
void spi_engine_bridge_sweep(void);
#endif
void ppos_set_tangenta(ena_type ena);
/* Вторая тангента: creg2.NPU_CTX_nCRX (CONT_TANG), линия на другую плату. */
void ppos_set_cont_tang(ena_type ena);




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
