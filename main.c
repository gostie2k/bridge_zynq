/******************************************************************************
* Copyright (C) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/
/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */


#include "cmd_udp.h"
#include "radio_stream.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include <stdio.h>
#include "platform.h"
#include "lwip/err.h"
#include "lwip/udp.h"
#include "netif/xadapter.h"
#include "xtime_l.h"
#include "xllfifo.h"
#include "xllfifo_hw.h"

#include "xil_printf.h"
#include "watchdog.h"
#include "platform_config.h"   /* FW_VERSION / WDT_ENABLED_DEFAULT */

#include "vid_buf.h"
#include "cmd_buf.h"

#include "xil_types.h"
#include "xsysmon.h"

#ifdef BRIDGE
#include "bridge_func.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "netif/bridgeif.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include "lwip/prot/ethernet.h"
#include "xemacps.h"
#include "netif/xemacpsif.h"
#endif

#include "xil_misc_psreset_api.h"
#include "xil_cache.h"
#include "modem.h"
#include "spi_engine/spi_engine.h"
#include "ad9361/no_os_spi.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"
#include "netif/bridgeif.h"
#include "firewall.h"




//struct circular_buffer_a;
vid_buf*   buf_vid;
vid_out_buf* vid_out;
struct vid_pkt* zero_vid_pkt;

cmd_buf*   buf_cmd;
cmd_out_buf* cmd_out;
struct cmd_pkt* zero_cmd_pkt;



struct ppos_tx tx_chan0;
struct ppos_rx rx_chan0;
struct ppos_par ppos;


uint32_t mean_dist;





//u32 temp_rx_words[47]; // Буфер для чтения 47 слов (188 байт) из FIFO
//    uint8_t temp_rx_block[188]; // Буфер для байтов
//    int rx_word_idx;

#define PL_TX_BLOCK_SIZE  188   // Размер блока для radio_input_block
#define PL_RX_MAX_SIZE    256   // Максимальный размер RX (с запасом)

u32     temp_rx_words[PL_RX_MAX_SIZE / 4];
uint8_t temp_rx_block[PL_RX_MAX_SIZE];
int     rx_word_idx;

u16 deb_cnt_br=0;
u16 deb_cnt_rf=0;

/* --- from _1: command source tracking --- */
cmd_source_t current_cmd_source = CMD_SRC_UART;
struct udp_pcb *global_reply_pcb = NULL;
ip_addr_t last_client_ip;
u16_t last_client_port;

/* --- from _1: SPI engine descriptors --- */
struct no_os_spi_desc*  spi_eng_ad9361;
struct no_os_spi_desc*  spi_eng_npu_com_board;
struct no_os_spi_desc*  spi_eng_npu_pow_board;

u8_t sys_hours = 0;
u8_t sys_minutes = 0;
u8_t sys_seconds = 0;




u16 deb_cnt_pkt=0;


u8 fpga_regs[43] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0};

//     24 bit start (8 sym) + 144 bit pream (48 sym ) + 24 bit stop (8 sym)
u8 pream_new[24] = {0x00, 0x00, 0x00,    0x28, 0x94, 0x4a, 0x25, 0x22, 0x4a, 0x48, 0xa4, 0x4a, 0x49, 0x12, 0x51, 0x44, 0xa2, 0x51, 0x29, 0x12, 0x52,    0x24, 0x92, 0x49};


// exterens from error_tracker.c
extern void err_reset();
extern void err_inc_recv_eth();
extern void err_inc_send_fifo();
extern void err_inc_recv_fifo();
extern void err_inc_send_eth();
extern void err_buff_write();
extern void err_group_buff_created();
extern void err_check();
extern unsigned long err_last_packet();
extern void err_inc_recv_fifo_wrong();
//extern void init_buf_a_header(capacity_buffer* _buff);
extern void init_platform();
extern void *no_os_calloc(size_t nitems, size_t size);
extern void platform_enable_interrupts();
extern void usleep(ULONG useconds);
extern void sleep(u32 seconds);

extern void bridgeif_fdb_age(void *fdb_ptr);
//extern struct netif bridge_netif;

extern unsigned long val_recv_eth;
extern unsigned long val_send_fifo;
extern unsigned long val_recv_fifo;
extern unsigned long val_recv_fifo_wrong;
extern unsigned long val_send_eth;
extern unsigned long val_buff_write;
extern unsigned long val_buff_read;
extern unsigned long val_group_buf;

//#define MAX_BUFFER_SIZE 6
//#define EXPECTED_PACKET_VID_SIZE 1212
//#define EXPECTED_PACKET_TEL_SIZE 1008
//#define EXPECTED_PACKET_RES_SIZE 5
//#define REQUEST_INTERVAL_MS 5000
//#define RESPONSE_TIMEOUT_MS 100

#define FIFO_READ_DELAY 20
#define FIFO_RX_DEPTH 24
#define FIFO_TX_DEPTH 32


//#define XLWIP_CONFIG_INCLUDE_GEM

typedef enum {
    WAITING_FOR_RESET_ACK,
    SENDING_REQUESTS,
	AWAIT_RESPONSE
} SystemState;

static XUartPs Uart1_Ps;		/* The instance of the UART Driver */
static XGpioPs Gpio_Ps;           /* The instance of the GPIO Driver */
static XScuGic ScuGic; // Interrupt Controller Driver
XLlFifo fifo;
XLlFifo fifo_deb;
static void IntrHandler(void *CallBackRef, u32 Bank, u32 Status);
static void exec_func(bool *func_start, u8 func_type, u8 *func_par);
static int debug_ram_load(u32 *bufff_cnt, uint8_t *bufff, XGpioPs *gpio_ptr, XUartPs *uart_ptr);
static u32 uart_cmd_chan(u32 cnt_uart_rx_buf, u32 *buf, XUartPs *uart_ptr, struct no_os_spi_desc *spi_desc_ptr,  bool *func_start, u8 *func_type, u8 *func_par, u8 *fpga_reg);
void lwip_init();


static void send_vid_packet_sized(uint8_t* vid, uint16_t size);


//void send_request();
//void send_reset_packet();
void recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
unsigned int swap32(unsigned int value);
unsigned short swap16(unsigned short value);
int init_tx_rx_fifo();
int init_deb_fifo();
//void send_debug_fifo_krt_gr_dat();
//void send_debug_fifo_krt_gr_dat_empty();
//void send_debug_rx_check(uint8_t ch_tx, uint8_t ch_rx);

bool is_run = false;

uint8_t *bufff;
u32 bufff_cnt=0;

//u32 rx_buf[2250];

u8 func_type;
u8 func_par[16];
bool func_start=false;


SystemState systemState = SENDING_REQUESTS;


#ifdef BRIDGE
 struct netif port1_bridge;
 //, port2_bridge,
 struct netif port3_bridge;
 struct netif bridge_netif;
 //struct netif udp_netif2;
#endif

#ifndef BRIDGE
 static struct netif server_netif;
#endif


struct netif *echo_netif;

struct udp_pcb *recv_pcb;
struct udp_pcb *debug_pcb;
struct udp_pcb *send_rx_data;

//u16_t RemotePort = 8;
u16_t RemotePortRx = 5018;
//u16_t RemotePortTxDebug = 28;
//u16_t RemotePortRxDebug = 58;
u16_t RemotePortDeb = 5078;

u16_t RemotePortStatus = 5007;


//------MERGE--------------
struct udp_pcb *cmd_udp_pcb;
struct udp_pcb *status_pcb;
u16_t CmdPort = 60012;  // порт для приема команд управления

u16_t StatusPort = 5008;
//------MERGE--------------



struct ip4_addr RemoteAddr;




//#pragma pack(push,1)
//struct krt_gr_dat
//{
//	uint8_t head[102];
//	uint8_t tel[1008];
//	uint8_t vid_0[1269];
//	uint8_t vid_1[1269];
//	uint8_t vid_2[1269];
//	uint8_t vid_3[1269];
//	uint8_t vid_4[1269];
//	uint8_t anal[62];
//	uint8_t code[1483];
//	uint8_t gr_size;
//	uint16_t gr_cnt;
//	uint8_t tel_is_val;
//	uint8_t vid_0_is_val;
//	uint8_t vid_1_is_val;
//	uint8_t vid_2_is_val;
//	uint8_t vid_3_is_val;
//	uint8_t vid_4_is_val;
//	uint8_t anal_is_val;
//	uint8_t rx_is_val;
//	uint8_t tmp;
//};
//#pragma pack(pop)



//#pragma pack(push,1)
//struct cmd_pkt
//{
//	uint8_t head[29];
//	uint8_t data[1518];
//	uint8_t code[253];
//	uint8_t pkt_size;
//	uint8_t is_val;
//	uint8_t rx_is_val;
//	uint8_t tmp;
//};
//#pragma pack(pop)




//#pragma pack(push,1)
//struct cmd_vid
//{
//	uint16_t  dcnt;
//	uint8_t   cmd;
//	uint16_t  cnt_gr;
//	uint8_t   type;
//	uint16_t  size;
//	uint8_t   cnt_pkt;
//	uint8_t   *dat_vid;
//};
//#pragma pack(pop)

//#pragma pack(push,1)
//struct cmd_tel
//{
//	uint16_t  dcnt;
//	uint8_t   cmd;
//	uint16_t  cnt_gr;
//	uint8_t   type;
//	uint16_t  size;
//	uint8_t   *dat_tel;
//};
//#pragma pack(pop)


XTime cur_time, mem_time ,last_response_time, tmp_time, max_tmp_time =0;

uint16_t ucnt = 0;
uint16_t cnt_gr = 0;
uint16_t cnt_gr_start_sent = FIFO_READ_DELAY;
//uint8_t first_send=1;

uint8_t wad=0;
uint8_t rad=0;

//struct krt_gr_dat fifo_krt_gr_dat[FIFO_TX_DEPTH];
//struct krt_gr_dat fifo_rx_buf[FIFO_RX_DEPTH];

struct vid_pkt fifo_krt_gr_dat[FIFO_TX_DEPTH];
struct vid_pkt fifo_rx_buf[FIFO_RX_DEPTH];

uint8_t fifo_rx_wad=0;
uint8_t fifo_rx_rad=0;


//uint16_t check_buf(struct krt_gr_dat* fifo_krt_gr_dat );
//void clear_val_buf(struct krt_gr_dat* fifo_krt_gr_dat , uint8_t ad);
//bool is_val_buf(struct krt_gr_dat* fifo_krt_gr_dat , uint8_t ad);
//void init_buf_header(struct krt_gr_dat* fifo_krt_gr_dat);
//void fifo_rx_buf_check(struct krt_gr_dat* rx_buf, uint8_t* rx_wad);
//void fifo_rx_buf_write(struct krt_gr_dat* rx_buf,uint8_t* rx_rad);


void fifo_rx_buf_check(struct vid_pkt* rx_buf, uint8_t* rx_wad);
void fifo_rx_buf_write(struct vid_pkt* rx_buf,uint8_t* rx_rad,  uint8_t* cmd_rx_buf);

//vid_pkt



//void send_tel_packet(uint8_t* tel);
//void send_vid_packet(uint8_t* vid);
void fifo_deb_read();
uint32_t deb_pkt_r = 0;
uint32_t deb_pkt_w = 0;
uint32_t deb_pkt_buf[4096];
void send_deb();
//void send_modem_status(struct stat_modem* status);

/* --- from _1: new functions --- */
void send_cmd();
void send_response_byte(XUartPs *uart_ptr, u8 byte);
void process_cmd_3bytes(uint8_t *data, XUartPs *uart_ptr, struct no_os_spi_desc *spi_desc_ptr, bool *func_start, u8 *func_type, u8 *func_par);

#define GPIO_OPS	        0 //&xil_gpio_ops
#define SPI_OPS		        0 //&xil_spi_ops
#define GPIO_PARAM	        0 //&xil_gpio_param
#define SPI_PARAM	        0 //&xil_spi_param
#define SPI_DEVICE_ID       0
#define NO_OS_SPI_MODE_1    0
#define SPI_CS              0


//#define MAX_BUFFER_SIZE 6
//#define EXPECTED_PACKET_SIZE 1269
//#define REQUEST_INTERVAL_MS 5000
//#define RESPONSE_TIMEOUT_MS 100

//typedef enum {
//    WAITING_FOR_RESET_ACK,
//    SENDING_REQUESTS
//} SystemState;







bool tx_seg_on = false;
bool rx_seg_on = false;








struct ad9361_rf_phy *ad9361_phy;
struct no_os_spi_desc desc_tmp;





#ifndef AXI_ADC_NOT_PRESENT

struct axi_adc_init rx_adc_init = {
	.name = "cf-ad9361-lpc",
	.base = RX_CORE_BASEADDR,
#ifdef FMCOMMS5
	.slave_base = RX_CORE_BASEADDR,
	.num_channels = 8,
#else
	.num_channels = 4,
#endif
	.num_slave_channels =  4
};
struct axi_dac_init tx_dac_init = {
	"cf-ad9361-dds-core-lpc",
	TX_CORE_BASEADDR,
	4,
	NULL,
	3
};

#endif

AD9361_InitParam default_init_param = {
	/* Device selection */
	ID_AD9361,	// dev_sel
	/* Reference Clock */
	40000000UL,	//reference_clk_rate
	/* Base Configuration */
	1,		//two_rx_two_tx_mode_enable *** adi,2rx-2tx-mode-enable
	1,		//one_rx_one_tx_mode_use_rx_num *** adi,1rx-1tx-mode-use-rx-num
	1,		//one_rx_one_tx_mode_use_tx_num *** adi,1rx-1tx-mode-use-tx-num
	1,		//frequency_division_duplex_mode_enable *** adi,frequency-division-duplex-mode-enable
	1,		//frequency_division_duplex_independent_mode_enable *** adi,frequency-division-duplex-independent-mode-enable
	0,		//tdd_use_dual_synth_mode_enable *** adi,tdd-use-dual-synth-mode-enable
	0,		//tdd_skip_vco_cal_enable *** adi,tdd-skip-vco-cal-enable
	0,		//tx_fastlock_delay_ns *** adi,tx-fastlock-delay-ns
	0,		//rx_fastlock_delay_ns *** adi,rx-fastlock-delay-ns
	0,		//rx_fastlock_pincontrol_enable *** adi,rx-fastlock-pincontrol-enable
	0,		//tx_fastlock_pincontrol_enable *** adi,tx-fastlock-pincontrol-enable
	0,		//external_rx_lo_enable *** adi,external-rx-lo-enable
	0,		//external_tx_lo_enable *** adi,external-tx-lo-enable
	5,		//dc_offset_tracking_update_event_mask *** adi,dc-offset-tracking-update-event-mask
	6,		//dc_offset_attenuation_high_range *** adi,dc-offset-attenuation-high-range
	5,		//dc_offset_attenuation_low_range *** adi,dc-offset-attenuation-low-range
	0x28,	//dc_offset_count_high_range *** adi,dc-offset-count-high-range
	0x32,	//dc_offset_count_low_range *** adi,dc-offset-count-low-range
	0,		//split_gain_table_mode_enable *** adi,split-gain-table-mode-enable
	MAX_SYNTH_FREF,	//trx_synthesizer_target_fref_overwrite_hz *** adi,trx-synthesizer-target-fref-overwrite-hz
	0,		// qec_tracking_slow_mode_enable *** adi,qec-tracking-slow-mode-enable
	/* ENSM Control */
	0,		//ensm_enable_pin_pulse_mode_enable *** adi,ensm-enable-pin-pulse-mode-enable
	0,		//ensm_enable_txnrx_control_enable *** adi,ensm-enable-txnrx-control-enable
	/* LO Control */
	3500000000UL,	//rx_synthesizer_frequency_hz *** adi,rx-synthesizer-frequency-hz
	3500000000UL,	//tx_synthesizer_frequency_hz *** adi,tx-synthesizer-frequency-hz
	0,				//tx_lo_powerdown_managed_enable *** adi,tx-lo-powerdown-managed-enable
	/* Rate & BW Control */
	//{983040000, 245760000, 122880000, 61440000, 30720000, 30720000},// rx_path_clock_frequencies[6] *** adi,rx-path-clock-frequencies
	//{983040000, 122880000, 122880000, 61440000, 30720000, 30720000},// tx_path_clock_frequencies[6] *** adi,tx-path-clock-frequencies
    //{768000000, 192000000, 64000000, 32000000, 16000000, 16000000},// rx_path_clock_frequencies[6] *** adi,rx-path-clock-frequencies
	//{768000000, 192000000, 64000000, 32000000, 16000000, 16000000},// tx_path_clock_frequencies[6] *** adi,tx-path-clock-frequencies
	//16000000,//rf_rx_bandwidth_hz *** adi,rf-rx-bandwidth-hz
	//16000000,//rf_tx_bandwidth_hz *** adi,rf-tx-bandwidth-hz

	//{1152000000, 144000000, 48000000, 24000000, 12000000, 12000000},// rx_path_clock_frequencies[6] *** adi,rx-path-clock-frequencies
    //{1152000000, 144000000, 48000000, 24000000, 12000000, 12000000},// tx_path_clock_frequencies[6] *** adi,tx-path-clock-frequencies

	{1152000000, 288000000, 96000000, 48000000, 24000000, 24000000}, // rx_path_clock_frequencies[6] *** adi,rx-path-clock-frequencies
    {1152000000, 288000000, 96000000, 48000000, 24000000, 24000000}, // tx_path_clock_frequencies[6] *** adi,tx-path-clock-frequencies

    //5000000,//rf_rx_bandwidth_hz *** adi,rf-rx-bandwidth-hz
    //5000000,//rf_tx_bandwidth_hz *** adi,rf-tx-bandwidth-hz

    12000000,//rf_rx_bandwidth_hz *** adi,rf-rx-bandwidth-hz
    12000000,//rf_tx_bandwidth_hz *** adi,rf-tx-bandwidth-hz

   #ifdef IS_ANTSDR
	/* RF Port Control */
	1,		//rx_rf_port_input_select *** adi,rx-rf-port-input-select
	1,		//tx_rf_port_input_select *** adi,tx-rf-port-input-select
   #endif

  #ifdef IS_MAKET_PKRV
	/* RF Port Control */
	0,		//rx_rf_port_input_select *** adi,rx-rf-port-input-select
	0,		//tx_rf_port_input_select *** adi,tx-rf-port-input-select
  #endif

  #ifdef IS_ZED_BOARD
   /* RF Port Control */
   0,		//rx_rf_port_input_select *** adi,rx-rf-port-input-select
   0,		//tx_rf_port_input_select *** adi,tx-rf-port-input-select
  #endif




	/* TX Attenuation Control */
	0, //50000,	//tx_attenuation_mdB *** adi,tx-attenuation-mdB
	0,		//update_tx_gain_in_alert_enable *** adi,update-tx-gain-in-alert-enable


#ifdef IS_ANTSDR
	/* Reference Clock Control */
	1,		//xo_disable_use_ext_refclk_enable *** adi,xo-disable-use-ext-refclk-enable
#endif

#ifdef IS_MAKET_PKRV
	/* Reference Clock Control */
	1,		//xo_disable_use_ext_refclk_enable *** adi,xo-disable-use-ext-refclk-enable
#endif


#ifdef IS_ZED_BOARD
	/* Reference Clock Control */
	0,		//xo_disable_use_ext_refclk_enable *** adi,xo-disable-use-ext-refclk-enable
#endif


	//{8, 2500},	//dcxo_coarse_and_fine_tune[2] *** adi,dcxo-coarse-and-fine-tune
	{8, 5920},	//dcxo_coarse_and_fine_tune[2] *** adi,dcxo-coarse-and-fine-tune

#ifdef IS_ZED_BOARD
	CLKOUT_ENABLE,	//clk_output_mode_select *** adi,clk-output-mode-select
#else
	CLKOUT_DISABLE,	//clk_output_mode_select *** adi,clk-output-mode-select
#endif



	/* Gain Control */
	1,		//gc_rx1_mode *** adi,gc-rx1-mode
	1,		//gc_rx2_mode *** adi,gc-rx2-mode
	//58,		//gc_adc_large_overload_thresh *** adi,gc-adc-large-overload-thresh
	58,		//gc_adc_large_overload_thresh *** adi,gc-adc-large-overload-thresh
	//4,		//gc_adc_ovr_sample_size *** adi,gc-adc-ovr-sample-size
	4,		//gc_adc_ovr_sample_size *** adi,gc-adc-ovr-sample-size
	//47,		//gc_adc_small_overload_thresh *** adi,gc-adc-small-overload-thresh
	47,		//gc_adc_small_overload_thresh *** adi,gc-adc-small-overload-thresh
	//8192,	//gc_dec_pow_measurement_duration *** adi,gc-dec-pow-measurement-duration
	//43200,	//gc_dec_pow_measurement_duration *** adi,gc-dec-pow-measurement-duration
	216,	//gc_dec_pow_measurement_duration *** adi,gc-dec-pow-measurement-duration
	0,		//gc_dig_gain_enable *** adi,gc-dig-gain-enable
	//800,	//gc_lmt_overload_high_thresh *** adi,gc-lmt-overload-high-thresh
	//704,	//gc_lmt_overload_low_thresh *** adi,gc-lmt-overload-low-thresh
	800,	//gc_lmt_overload_high_thresh *** adi,gc-lmt-overload-high-thresh
	704,	//gc_lmt_overload_low_thresh *** adi,gc-lmt-overload-low-thresh
	//24,		//gc_low_power_thresh *** adi,gc-low-power-thresh
	24,		//gc_low_power_thresh *** adi,gc-low-power-thresh
	15,		//gc_max_dig_gain *** adi,gc-max-dig-gain
	0,		//gc_use_rx_fir_out_for_dec_pwr_meas_enable *** adi,gc-use-rx-fir-out-for-dec-pwr-meas-enable
	/* Gain MGC Control */
	2,		//mgc_dec_gain_step *** adi,mgc-dec-gain-step
	2,		//mgc_inc_gain_step *** adi,mgc-inc-gain-step
	0,		//mgc_rx1_ctrl_inp_enable *** adi,mgc-rx1-ctrl-inp-enable
	0,		//mgc_rx2_ctrl_inp_enable *** adi,mgc-rx2-ctrl-inp-enable
	0,		//mgc_split_table_ctrl_inp_gain_mode *** adi,mgc-split-table-ctrl-inp-gain-mode
	/* Gain AGC Control */
	//10,		//agc_adc_large_overload_exceed_counter *** adi,agc-adc-large-overload-exceed-counter
	//2,		//agc_adc_large_overload_inc_steps *** adi,agc-adc-large-overload-inc-steps

	10,		//agc_adc_large_overload_exceed_counter *** adi,agc-adc-large-overload-exceed-counter
	2,		//agc_adc_large_overload_inc_steps *** adi,agc-adc-large-overload-inc-steps


	//0,		//agc_adc_lmt_small_overload_prevent_gain_inc_enable *** adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable
	0,		//agc_adc_lmt_small_overload_prevent_gain_inc_enable *** adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable
	10,		//agc_adc_small_overload_exceed_counter *** adi,agc-adc-small-overload-exceed-counter

	4,		//agc_dig_gain_step_size *** adi,agc-dig-gain-step-size
	3,		//agc_dig_saturation_exceed_counter *** adi,agc-dig-saturation-exceed-counter
	//10000,	// agc_gain_update_interval_us *** adi,agc-gain-update-interval-us
	10000,	// agc_gain_update_interval_us *** adi,agc-gain-update-interval-us
	0,		//agc_immed_gain_change_if_large_adc_overload_enable *** adi,agc-immed-gain-change-if-large-adc-overload-enable
	0,		//agc_immed_gain_change_if_large_lmt_overload_enable *** adi,agc-immed-gain-change-if-large-lmt-overload-enable
	10,		//agc_inner_thresh_high *** adi,agc-inner-thresh-high
	1,		//agc_inner_thresh_high_dec_steps *** adi,agc-inner-thresh-high-dec-steps
	12,		//agc_inner_thresh_low *** adi,agc-inner-thresh-low
	1,		//agc_inner_thresh_low_inc_steps *** adi,agc-inner-thresh-low-inc-steps
	10,		//agc_lmt_overload_large_exceed_counter *** adi,agc-lmt-overload-large-exceed-counter
	//2,		//agc_lmt_overload_large_inc_steps *** adi,agc-lmt-overload-large-inc-steps
	2,		//agc_lmt_overload_large_inc_steps *** adi,agc-lmt-overload-large-inc-steps
	10,		//agc_lmt_overload_small_exceed_counter *** adi,agc-lmt-overload-small-exceed-counter
	5,		//agc_outer_thresh_high *** adi,agc-outer-thresh-high
	2,		//agc_outer_thresh_high_dec_steps *** adi,agc-outer-thresh-high-dec-steps
	18,		//agc_outer_thresh_low *** adi,agc-outer-thresh-low
	2,		//agc_outer_thresh_low_inc_steps *** adi,agc-outer-thresh-low-inc-steps
	//1,		//agc_attack_delay_extra_margin_us; *** adi,agc-attack-delay-extra-margin-us
	1,		//agc_attack_delay_extra_margin_us; *** adi,agc-attack-delay-extra-margin-us
	0,		//agc_sync_for_gain_counter_enable *** adi,agc-sync-for-gain-counter-enable
	/* Fast AGC */
	//64,		//fagc_dec_pow_measuremnt_duration ***  adi,fagc-dec-pow-measurement-duration

	//43200,		//fagc_dec_pow_measuremnt_duration ***  adi,fagc-dec-pow-measurement-duration
	216,		//fagc_dec_pow_measuremnt_duration ***  adi,fagc-dec-pow-measurement-duration
	560,	//fagc_state_wait_time_ns ***  adi,fagc-state-wait-time-ns




	/* Fast AGC - Low Power */
	//0,		//fagc_allow_agc_gain_increase ***  adi,fagc-allow-agc-gain-increase-enable
	0,		//fagc_allow_agc_gain_increase ***  adi,fagc-allow-agc-gain-increase-enable
	5,		//fagc_lp_thresh_increment_time ***  adi,fagc-lp-thresh-increment-time
	1,		//fagc_lp_thresh_increment_steps ***  adi,fagc-lp-thresh-increment-steps
	/* Fast AGC - Lock Level (Lock Level is set via slow AGC inner high threshold) */
	//1,		//fagc_lock_level_lmt_gain_increase_en ***  adi,fagc-lock-level-lmt-gain-increase-enable
	1,		//fagc_lock_level_lmt_gain_increase_en ***  adi,fagc-lock-level-lmt-gain-increase-enable
	5,		//fagc_lock_level_gain_increase_upper_limit ***  adi,fagc-lock-level-gain-increase-upper-limit
	/* Fast AGC - Peak Detectors and Final Settling */
	1,		//fagc_lpf_final_settling_steps ***  adi,fagc-lpf-final-settling-steps
	1,		//fagc_lmt_final_settling_steps ***  adi,fagc-lmt-final-settling-steps
	3,		//fagc_final_overrange_count ***  adi,fagc-final-overrange-count
	/* Fast AGC - Final Power Test */
	0,		//fagc_gain_increase_after_gain_lock_en ***  adi,fagc-gain-increase-after-gain-lock-enable
	/* Fast AGC - Unlocking the Gain */
	0,		//fagc_gain_index_type_after_exit_rx_mode ***  adi,fagc-gain-index-type-after-exit-rx-mode
	0,		//fagc_use_last_lock_level_for_set_gain_en ***  adi,fagc-use-last-lock-level-for-set-gain-enable
	1,		//fagc_rst_gla_stronger_sig_thresh_exceeded_en ***  adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable
	5,		//fagc_optimized_gain_offset ***  adi,fagc-optimized-gain-offset
	10,		//fagc_rst_gla_stronger_sig_thresh_above_ll ***  adi,fagc-rst-gla-stronger-sig-thresh-above-ll
	1,		//fagc_rst_gla_engergy_lost_sig_thresh_exceeded_en ***  adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable
	1,		//fagc_rst_gla_engergy_lost_goto_optim_gain_en ***  adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable
	10,		//fagc_rst_gla_engergy_lost_sig_thresh_below_ll ***  adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll
	8,		//fagc_energy_lost_stronger_sig_gain_lock_exit_cnt ***  adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt
	1,		//fagc_rst_gla_large_adc_overload_en ***  adi,fagc-rst-gla-large-adc-overload-enable
	1,		//fagc_rst_gla_large_lmt_overload_en ***  adi,fagc-rst-gla-large-lmt-overload-enable
	0,		//fagc_rst_gla_en_agc_pulled_high_en ***  adi,fagc-rst-gla-en-agc-pulled-high-enable
	0,		//fagc_rst_gla_if_en_agc_pulled_high_mode ***  adi,fagc-rst-gla-if-en-agc-pulled-high-mode
	//64,		//fagc_power_measurement_duration_in_state5 ***  adi,fagc-power-measurement-duration-in-state5
	216,		//fagc_power_measurement_duration_in_state5 ***  adi,fagc-power-measurement-duration-in-state5

	//2,		//fagc_large_overload_inc_steps *** adi,fagc-adc-large-overload-inc-steps
	2,		//fagc_large_overload_inc_steps *** adi,fagc-adc-large-overload-inc-steps

	/* RSSI Control */
	1,		//rssi_delay *** adi,rssi-delay
	//10000,	//rssi_duration *** adi,rssi-duration
	1000,	//rssi_duration *** adi,rssi-duration
	3,		//rssi_restart_mode *** adi,rssi-restart-mode
	0,		//rssi_unit_is_rx_samples_enable *** adi,rssi-unit-is-rx-samples-enable
	1,		//rssi_wait *** adi,rssi-wait
	/* Aux ADC Control */
	256,	//aux_adc_decimation *** adi,aux-adc-decimation
	40000000UL,	//aux_adc_rate *** adi,aux-adc-rate
	/* AuxDAC Control */
	0,		//aux_dac_manual_mode_enable ***  adi,aux-dac-manual-mode-enable
	0,		//aux_dac1_default_value_mV ***  adi,aux-dac1-default-value-mV
	0,		//aux_dac1_active_in_rx_enable ***  adi,aux-dac1-active-in-rx-enable
	0,		//aux_dac1_active_in_tx_enable ***  adi,aux-dac1-active-in-tx-enable
	0,		//aux_dac1_active_in_alert_enable ***  adi,aux-dac1-active-in-alert-enable
	0,		//aux_dac1_rx_delay_us ***  adi,aux-dac1-rx-delay-us
	0,		//aux_dac1_tx_delay_us ***  adi,aux-dac1-tx-delay-us
	0,		//aux_dac2_default_value_mV ***  adi,aux-dac2-default-value-mV
	0,		//aux_dac2_active_in_rx_enable ***  adi,aux-dac2-active-in-rx-enable
	0,		//aux_dac2_active_in_tx_enable ***  adi,aux-dac2-active-in-tx-enable
	0,		//aux_dac2_active_in_alert_enable ***  adi,aux-dac2-active-in-alert-enable
	0,		//aux_dac2_rx_delay_us ***  adi,aux-dac2-rx-delay-us
	0,		//aux_dac2_tx_delay_us ***  adi,aux-dac2-tx-delay-us
	/* Temperature Sensor Control */
	256,	//temp_sense_decimation *** adi,temp-sense-decimation
	1000,	//temp_sense_measurement_interval_ms *** adi,temp-sense-measurement-interval-ms
	0xCE,	//temp_sense_offset_signed *** adi,temp-sense-offset-signed
	1,		//temp_sense_periodic_measurement_enable *** adi,temp-sense-periodic-measurement-enable
	/* Control Out Setup */
	0xFF,	//ctrl_outs_enable_mask *** adi,ctrl-outs-enable-mask
	0,		//ctrl_outs_index *** adi,ctrl-outs-index
	/* External LNA Control */
	0,		//elna_settling_delay_ns *** adi,elna-settling-delay-ns
	0,		//elna_gain_mdB *** adi,elna-gain-mdB
	0,		//elna_bypass_loss_mdB *** adi,elna-bypass-loss-mdB
	0,		//elna_rx1_gpo0_control_enable *** adi,elna-rx1-gpo0-control-enable
	0,		//elna_rx2_gpo1_control_enable *** adi,elna-rx2-gpo1-control-enable
	0,		//elna_gaintable_all_index_enable *** adi,elna-gaintable-all-index-enable
	/* Digital Interface Control */
	0,		//digital_interface_tune_skip_mode *** adi,digital-interface-tune-skip-mode
	1,		//digital_interface_tune_fir_disable *** adi,digital-interface-tune-fir-disable
	1,		//pp_tx_swap_enable *** adi,pp-tx-swap-enable
	1,		//pp_rx_swap_enable *** adi,pp-rx-swap-enable
	0,		//tx_channel_swap_enable *** adi,tx-channel-swap-enable
	0,		//rx_channel_swap_enable *** adi,rx-channel-swap-enable

	1,		//rx_frame_pulse_mode_enable *** adi,rx-frame-pulse-mode-enable

	1,		//two_t_two_r_timing_enable *** adi,2t2r-timing-enable
	0,		//invert_data_bus_enable *** adi,invert-data-bus-enable
	0,		//invert_data_clk_enable *** adi,invert-data-clk-enable
	0,		//fdd_alt_word_order_enable *** adi,fdd-alt-word-order-enable
	0,		//invert_rx_frame_enable *** adi,invert-rx-frame-enable
	0,		//fdd_rx_rate_2tx_enable *** adi,fdd-rx-rate-2tx-enable
	1,		//swap_ports_enable *** adi,swap-ports-enable
	0,		//single_data_rate_enable *** adi,single-data-rate-enable
	1,		//lvds_mode_enable *** adi,lvds-mode-enable
	0,		//half_duplex_mode_enable *** adi,half-duplex-mode-enable
	0,		//single_port_mode_enable *** adi,single-port-mode-enable
	0,		//full_port_enable *** adi,full-port-enable
	0,		//full_duplex_swap_bits_enable *** adi,full-duplex-swap-bits-enable
	0,		//delay_rx_data *** adi,delay-rx-data
	0,		//rx_data_clock_delay *** adi,rx-data-clock-delay
	4,		//rx_data_delay *** adi,rx-data-delay
	7,		//tx_fb_clock_delay *** adi,tx-fb-clock-delay
	0,		//tx_data_delay *** adi,tx-data-delay
#ifdef ALTERA_PLATFORM
	300,	//lvds_bias_mV *** adi,lvds-bias-mV
#else
	150,	//lvds_bias_mV *** adi,lvds-bias-mV
#endif
	1,		//lvds_rx_onchip_termination_enable *** adi,lvds-rx-onchip-termination-enable
	0,		//rx1rx2_phase_inversion_en *** adi,rx1-rx2-phase-inversion-enable
	0xFF,	//lvds_invert1_control *** adi,lvds-invert1-control
	0x0F,	//lvds_invert2_control *** adi,lvds-invert2-control
	/* GPO Control */
	0,		//gpo_manual_mode_enable *** adi,gpo-manual-mode-enable
	0,		//gpo_manual_mode_enable_mask *** adi,gpo-manual-mode-enable-mask
	0,		//gpo0_inactive_state_high_enable *** adi,gpo0-inactive-state-high-enable
	0,		//gpo1_inactive_state_high_enable *** adi,gpo1-inactive-state-high-enable
	0,		//gpo2_inactive_state_high_enable *** adi,gpo2-inactive-state-high-enable
	0,		//gpo3_inactive_state_high_enable *** adi,gpo3-inactive-state-high-enable
	0,		//gpo0_slave_rx_enable *** adi,gpo0-slave-rx-enable
	0,		//gpo0_slave_tx_enable *** adi,gpo0-slave-tx-enable
	0,		//gpo1_slave_rx_enable *** adi,gpo1-slave-rx-enable
	0,		//gpo1_slave_tx_enable *** adi,gpo1-slave-tx-enable
	0,		//gpo2_slave_rx_enable *** adi,gpo2-slave-rx-enable
	0,		//gpo2_slave_tx_enable *** adi,gpo2-slave-tx-enable
	0,		//gpo3_slave_rx_enable *** adi,gpo3-slave-rx-enable
	0,		//gpo3_slave_tx_enable *** adi,gpo3-slave-tx-enable
	0,		//gpo0_rx_delay_us *** adi,gpo0-rx-delay-us
	0,		//gpo0_tx_delay_us *** adi,gpo0-tx-delay-us
	0,		//gpo1_rx_delay_us *** adi,gpo1-rx-delay-us
	0,		//gpo1_tx_delay_us *** adi,gpo1-tx-delay-us
	0,		//gpo2_rx_delay_us *** adi,gpo2-rx-delay-us
	0,		//gpo2_tx_delay_us *** adi,gpo2-tx-delay-us
	0,		//gpo3_rx_delay_us *** adi,gpo3-rx-delay-us
	0,		//gpo3_tx_delay_us *** adi,gpo3-tx-delay-us
	/* Tx Monitor Control */
	37000,	//low_high_gain_threshold_mdB *** adi,txmon-low-high-thresh
	0,		//low_gain_dB *** adi,txmon-low-gain
	24,		//high_gain_dB *** adi,txmon-high-gain
	0,		//tx_mon_track_en *** adi,txmon-dc-tracking-enable
	0,		//one_shot_mode_en *** adi,txmon-one-shot-mode-enable
	511,	//tx_mon_delay *** adi,txmon-delay
	8192,	//tx_mon_duration *** adi,txmon-duration
	2,		//tx1_mon_front_end_gain *** adi,txmon-1-front-end-gain
	2,		//tx2_mon_front_end_gain *** adi,txmon-2-front-end-gain
	48,		//tx1_mon_lo_cm *** adi,txmon-1-lo-cm
	48,		//tx2_mon_lo_cm *** adi,txmon-2-lo-cm
	/* GPIO definitions */
	{
		.number = -1,
		.platform_ops = GPIO_OPS,
		.extra = GPIO_PARAM
	},		//gpio_resetb *** reset-gpios
	/* MCS Sync */
	{
		.number = -1,
		.platform_ops = GPIO_OPS,
		.extra = GPIO_PARAM
	},		//gpio_sync *** sync-gpios

	{
		.number = -1,
		.platform_ops = GPIO_OPS,
		.extra = GPIO_PARAM
	},		//gpio_cal_sw1 *** cal-sw1-gpios

	{
		.number = -1,
		.platform_ops = GPIO_OPS,
		.extra = GPIO_PARAM
	},		//gpio_cal_sw2 *** cal-sw2-gpios

	{
		.device_id = SPI_DEVICE_ID,
		.mode = NO_OS_SPI_MODE_1,
		.chip_select = SPI_CS,
		.platform_ops = SPI_OPS,
		.extra = SPI_PARAM
	},

	/* External LO clocks */
	NULL,	//(*ad9361_rfpll_ext_recalc_rate)()
	NULL,	//(*ad9361_rfpll_ext_round_rate)()
	NULL,	//(*ad9361_rfpll_ext_set_rate)()
#ifndef AXI_ADC_NOT_PRESENT
	&rx_adc_init,	// *rx_adc_init
	&tx_dac_init,   // *tx_dac_init
#endif
};


AD9361_RXFIRConfig rx_fir_config = {	// BPF PASSBAND 3/20 fs to 1/4 fs
	3, // rx
	0, // rx_gain
	1, // rx_dec
	{
//	    33,   -20,   -20,    41,    -8,   -47,    42,    30,   //rcosdesign(.22, 15, 2,'sqrt')
//	   -64,    10,    59,   -63,   -15,   106,   -61,  -110,
//	   146,    50,  -192,    91,   130,  -311,   125,   589,
//	  -692,  -885,  1790,  1147, -4153, -1328, 14484, 24563,
//	 14484, -1328, -4153,  1147,  1790,  -885,  -692,   589,
//	   125,  -311,   130,    91,  -192,    50,   146,  -110,
//	   -61,   106,   -15,   -63,    59,    10,   -64,    30,
//	    42,   -47,    -8,    41,   -20,   -20,    33,     0,

//	   103,   118,    36,   -82,  -136,   -71,    64,   150,  //rcosdesign(.22, 15, 4,'sqrt')
//      92,   -81,  -220,  -170,    89,   374,   417,    76,
//    -489,  -858,  -626,   244,  1265,  1646,   811, -1069,
//	 -2937, -3273,  -939,  4041, 10242, 15379, 17369, 15379,
//	 10242,  4041,  -939, -3273, -2937, -1069,   811,  1646,
//	  1265,   244,  -626,  -858,  -489,    76,   417,   374,
//	    89,  -170,  -220,   -81,    92,   150,    64,   -71,
//	  -136,   -82,    36,   118,   103,     0,     0,     0,

//	  -278,  -134,   200,   486,   448,     0,  -622,  -954, //rcosdesign(.2, 10, 4,'sqrt')
//	  -618,   348,  1391,  1703,   763, -1181, -3027, -3277,
//	  -861,  4134, 10276, 15329, 17282, 15329, 10276,  4134,
//	  -861, -3277, -3027, -1181,   763,  1703,  1391,   348,
//	  -618,  -954,  -622,     0,   448,   486,   200,  -134,
//	  -278,     0,     0,     0,     0,     0,     0,     0,
//		 0,     0,     0,     0,     0,     0,     0,     0,
//		 0,     0,     0,     0,     0,     0,     0,     0,


		   -1,    -2,     3,     5,    -8,   -11,    16,    21,  //hbf_24_12_coef_short.dat
		  -30,   -38,    51,    63,   -82,   -99,   126,   151,
		 -187,  -222,   272,   321,  -390,  -459,   555,   658,
		 -800,  -965,  1202,  1516, -2017, -2878,  4880, 14733,
		14733,  4880, -2878, -2017,  1516,  1202,  -965,  -800,
		  658,   555,  -459,  -390,   321,   272,  -222,  -187,
		  151,   126,   -99,   -82,    63,    51,   -38,   -30,
		   21,    16,   -11,    -8,     5,     3,    -2,    -1,








		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0}, // rx_coef[64]
	64, // rx_coef_size
	{0, 0, 0, 0, 0, 0}, //rx_path_clks[6]
	0 // rx_bandwidth
};

AD9361_TXFIRConfig tx_fir_config = {	// BPF PASSBAND 3/20 fs to 1/4 fs
	3, // tx
	0, // tx_gain
	1, // tx_int
	{
//	    33,   -20,   -20,    41,    -8,   -47,    42,    30,   //rcosdesign(.22, 15, 2,'sqrt')
//	   -64,    10,    59,   -63,   -15,   106,   -61,  -110,
//	   146,    50,  -192,    91,   130,  -311,   125,   589,
//	  -692,  -885,  1790,  1147, -4153, -1328, 14484, 24563,
//	 14484, -1328, -4153,  1147,  1790,  -885,  -692,   589,
//	   125,  -311,   130,    91,  -192,    50,   146,  -110,
//	   -61,   106,   -15,   -63,    59,    10,   -64,    30,
//	    42,   -47,    -8,    41,   -20,   -20,    33,     0,
//	   103,   118,    36,   -82,  -136,   -71,    64,   150,  //rcosdesign(.22, 15, 4,'sqrt')
//      92,   -81,  -220,  -170,    89,   374,   417,    76,
//    -489,  -858,  -626,   244,  1265,  1646,   811, -1069,
//	 -2937, -3273,  -939,  4041, 10242, 15379, 17369, 15379,
//	 10242,  4041,  -939, -3273, -2937, -1069,   811,  1646,
//	  1265,   244,  -626,  -858,  -489,    76,   417,   374,
//	    89,  -170,  -220,   -81,    92,   150,    64,   -71,
//	  -136,   -82,    36,   118,   103,     0,     0,     0,

	 // -278,  -134,   200,   486,   448,     0,  -622,  -954, //rcosdesign(.2, 10, 4,'sqrt')
	 // -618,   348,  1391,  1703,   763, -1181, -3027, -3277,
	 // -861,  4134, 10276, 15329, 17282, 15329, 10276,  4134,
	 // -861, -3277, -3027, -1181,   763,  1703,  1391,   348,
	 // -618,  -954,  -622,     0,   448,   486,   200,  -134,
	 // -278,     0,     0,     0,     0,     0,     0,     0,

//	  -132,   -64,    95,   231,   212,     0,  -295,  -452,
//    -293,   165,   659,   807,   362,  -560, -1435, -1553,
//    -408,  1959,  4871,  7266,  8192,  7266,  4871,  1959,
//    -408, -1553, -1435,  -560,   362,   807,   659,   165,
//    -293,  -452,  -295,     0,   212,   231,    95,   -64,
//    -132,     0,     0,     0,     0,     0,     0,     0,




	   -1,    -2,     3,     5,    -8,   -11,    16,    21,  //hbf_24_12_coef_short.dat
	  -30,   -38,    51,    63,   -82,   -99,   126,   151,
	 -187,  -222,   272,   321,  -390,  -459,   555,   658,
	 -800,  -965,  1202,  1516, -2017, -2878,  4880, 14733,
	14733,  4880, -2878, -2017,  1516,  1202,  -965,  -800,
	  658,   555,  -459,  -390,   321,   272,  -222,  -187,
	  151,   126,   -99,   -82,    63,    51,   -38,   -30,
	   21,    16,   -11,    -8,     5,     3,    -2,    -1,





	//	 0,     0,     0,     0,     0,     0,     0,     0,
	//	 0,     0,     0,     0,     0,     0,     0,     0,

		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0,
		 0,     0,     0,     0,     0,     0,     0,     0}, // tx_coef[64]
	64, // tx_coef_size
	{0, 0, 0, 0, 0, 0}, // tx_path_clks[6]
	0 // tx_bandwidth
};
//struct ad9361_rf_phy *ad9361_phy;
//struct no_os_spi_desc desc_tmp;


//struct rf_rssi read_rssi;
//bool stt = false;

uint16_t cnt_s_pkt = 0;
bool is_cntrl_pkt_end = false;
float pow_cntrl_tx;
float pow_rx;
uint8_t gain_index_rx;
bool is_pow_rx = false;
bool is_pow_cntrl_tx = false;












































































static void IntrHandler(void *CallBackRef, u32
Bank, u32 Status)
{
 u32 size;
 XGpioPs *Gpioint = (XGpioPs *) CallBackRef;

 XGpioPs_IntrClear(Gpioint, Bank, Status);


if (  ((Status&0x20000000) == 0x20000000)&&(Bank == 3))
{
/*
  uint8_t d[] = {0x01,0x61,0x00,0x00};
  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
  //xil_printf("%02X ", d[2]);

  float rssi = -d[2];
  rssi/=4.;

  d[0] = 0x02; d[1] = 0xb0;
  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
  //xil_printf("%02X", d[2]);

  rssi-=d[2];

  gain_index_rx = d[2];

  pow_rx = rssi + 13.9397;//3500 tone corection



  ppos.creg7.inside.del_ack = Xil_In32(AXI_CNTRL_REG_BASEADDR + 7*4);
*/

  is_pow_rx = true;

 // printf("#1 %.2f dB \n\r", rssi);


//if (stt)
//	  {
//	  ad9361_get_rx_rssi(ad9361_phy, 0, &read_rssi);
//    uint32_t rssi_db;
//    uint8_t rssi_db_mod;
//    printf("symbol = %d, preamble = %d, multiplier = %d, duration = %d\n\r", read_rssi.symbol, read_rssi.preamble, read_rssi.multiplier, read_rssi.duration);
//    rssi_db = read_rssi.symbol / 1000;
//    rssi_db_mod = (read_rssi.symbol % 1000)/100;
//    printf("rssi_db = %d.%d \n\r",  rssi_db, rssi_db_mod);
//}
//xil_printf("%08X%08X\n\r", Bank, Status);

  return;
}




//if (  ((Status&0x10000000) == 0x10000000)&&(Bank == 3))
//{
//  XGpioPs_IntrClearPin(Gpioint, 54+60);
/*
  uint8_t d[] = {0x01,0x63,0x00,0x00};
  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
  //xil_printf("%02X ", d[2]);
  //XUartPs_SendByte(Uart1_Ps.Config.BaseAddress,d[2]);

  float rssi = -d[2];
  rssi/=4.;

  d[0] = 0x02; d[1] = 0xb5;
  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
  //xil_printf("%02X", d[2]);
  //XUartPs_SendByte(Uart1_Ps.Config.BaseAddress,d[2]);

  rssi-=d[2];

  pow_cntrl_tx = rssi;
  is_pow_cntrl_tx = true;

 // printf("#2 %.2f dB \n\r", rssi);
*/
//  return;
//}



//if (  ((Status&0x08000000) == 0x08000000)&&(Bank == 3))
//{
// XGpioPs_IntrClearPin(Gpioint, 54+59);
/*
 is_cntrl_pkt_end = true;
 */
// return;
//}



//if (((Status&0x80000000) == 0x80000000)&&(Bank == 3))
//{

// XGpioPs_IntrClearPin(Gpioint, 54+63);

//if (!is_run)
//	return;

//u16 tx_len;
//tx_len=fpga_regs[12];
//tx_len<<=8;
//tx_len+=fpga_regs[13];


//}


}






u32_t sys_now(void) {
    XTime tCur;
    XTime_GetTime(&tCur);
    // Преобразуем такты таймера (counts) в миллисекунды
    // Частота таймера в Zynq обычно COUNTS_PER_SECOND = CPU_3x2x_CLK / 2
    return (u32_t)(tCur / (COUNTS_PER_SECOND / 1000));
}











static XSysMon XADCMonInst;
int counter_err = 0;
struct stat_modem* status_modem;
bool led_tog =true;


int main()
{
	/* Прочитать SLCR.REBOOT_STATUS до любого ресета регистров — покажет, был ли
	 * последний ресет от watchdog (crash_section). */
	wdt_capture_reboot_status();

	status_modem = 	stat_modem_init();


	//struct ppos_par tmpp;
    //tmpp.creg0.word



	uint8_t cmd_rx_buf[1800];

 	buf_vid  = vid_buf_init();
    vid_out = vid_out_buf_init();
    zero_vid_pkt = zero_vid_init();
    err_reset();

    buf_cmd=cmd_buf_init();
    cmd_out=cmd_out_buf_init();
    zero_cmd_pkt = zero_cmd_init();

#ifdef BRIDGE
    echo_netif = &port1_bridge;
#endif



    u32 cnt_uart_rx_buf=0;
	u32 buf=0;

	int status = 0;
	(void)status;





    // init_platform();


 	 Xil_ICacheEnable();
 	 Xil_DCacheEnable();

 	 platform_setup_timer();


 	 knk_init_platform(&Uart1_Ps, &Gpio_Ps);//, &ScuGic, IntrHandler);


 	 platform_setup_interrupts(&ScuGic,&Gpio_Ps, IntrHandler);









	XSysMon_Config *ConfigPtr;

	XSysMon *XADCInstPtr = &XADCMonInst;
    int Status_ADC;

    ConfigPtr = XSysMon_LookupConfig(0);
    	if (ConfigPtr == NULL) {
    		return XST_FAILURE;
    	}
	Status_ADC = XSysMon_CfgInitialize(XADCInstPtr,ConfigPtr,ConfigPtr->BaseAddress);
	if(XST_SUCCESS != Status_ADC){
		print("ADC INIT FAILED\n\r");
		return XST_FAILURE;
	 }


    XGpioPs_SetDirectionPin(&Gpio_Ps, 14, 1);
    XGpioPs_SetOutputEnablePin(&Gpio_Ps, 14, 1);

    XGpioPs_SetDirectionPin(&Gpio_Ps, 15, 1);
    XGpioPs_SetOutputEnablePin(&Gpio_Ps, 15, 1);

    XGpioPs_SetDirectionPin(&Gpio_Ps, 46, 1);
    XGpioPs_SetOutputEnablePin(&Gpio_Ps, 46, 1);

    XGpioPs_WritePin(&Gpio_Ps, 46, 1);



    //for (int k=0; k<1; k++)
    //{
    //XGpioPs_WritePin(&Gpio_Ps, 15, 0);
    //sleep(1);
    //XGpioPs_WritePin(&Gpio_Ps, 15, 1);
    //sleep(1);
    //}


    XGpioPs_SetDirectionPin(&Gpio_Ps, 46, 1);
    XGpioPs_SetOutputEnablePin(&Gpio_Ps, 46, 1);
    XGpioPs_WritePin(&Gpio_Ps, 46, 1);
    //sleep(1);
    //XGpioPs_WritePin(&Gpio_Ps, 46, 0);
    //sleep(2);
    //XGpioPs_WritePin(&Gpio_Ps, 46, 1);



    if (init_tx_rx_fifo() != XST_SUCCESS) {
        xil_printf("FIFO initialization failed.\n\r");
        return -1;
    }

    if (init_deb_fifo() != XST_SUCCESS) {
        xil_printf("FIFO initialization failed.\n\r");
        return -1;
    }

    bufff=(u8*)no_os_calloc(1,12288);
    desc_tmp.Gpio=&Gpio_Ps;
    struct no_os_spi_init_param spi_init_param;
    spi_init_param.parent=&desc_tmp;
    default_init_param.spi_param=spi_init_param;
    XGpioPs_WritePin(&Gpio_Ps, 54+31, 1); //init_done
    XGpioPs_WritePin(&Gpio_Ps, 54+25, 1);





#ifdef   IS_MAKET_PKRV


//start merge
    u8 dac_buf[3];
    dac_buf [0] = 0x84;
    dac_buf [1] = 0x27;
    dac_buf [2] = 0x3f;//0x7f;

    no_os_spi_write_and_read_11bit(&desc_tmp, dac_buf, 1);

    dac_buf [0] = 0x84;
    dac_buf [1] = 0x26;
    dac_buf [2] = 0x3b; //0x3b; // write & update
    //dac_buf [2] = 0x40; //power down -> output tri-state

    no_os_spi_write_and_read_11bit(&desc_tmp, dac_buf, 1);

    dac_buf [0] = 0x84;
    dac_buf [1] = 0x0;
    dac_buf [2] = 0x10;

    no_os_spi_write_and_read_11bit(&desc_tmp, &dac_buf[0], 1);

    usleep(150);

//end merge


#endif










    //struct spi_engine_init_param spi_engine_init_par;
    //spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_AD9361_BASEADDR;
    //spi_engine_init_par.type = SPI_ENGINE;
    //spi_engine_init_par.data_width = 8;
    //spi_engine_init_par.cs_delay = 0;
    //spi_engine_init_par.ref_clk_hz = 100000000;
    //spi_engine_init_par.sdo_idle_state = 1;


    //struct no_os_spi_init_param no_os_spi_init;
    //no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    //no_os_spi_init.mode=1;//NO_OS_SPI_MODE_1;
    //no_os_spi_init.device_id = 0;
    //no_os_spi_init.chip_select = 0;


    ////struct no_os_spi_desc*  spi_eng;
    //spi_engine_init(&spi_eng, &no_os_spi_init);



    ////uint8_t ppp = 0xf0;
    ////spi_engine_write_and_read(spi_eng, &ppp, 1);



     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, 0x00010000);

     spi_engine_npu_com_board_init(&spi_eng_npu_com_board);
     spi_engine_npu_pow_board_init(&spi_eng_npu_pow_board);

     #ifdef  IS_BORT

     ppos.creg2.inside.Pit = 1;
	 ppos.creg2.inside.Fdiap = 0;
	 ppos.creg2.inside.GPIOnR_TYPE = 2;

	 ppos.creg2.inside.Tang = 0;
	 ppos.creg2.inside.Tang_avt = 0;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);

     #endif








    ppos.creg3.word = 0;
    ppos.creg3.inside.AD9361_ENABLE =1;
	ppos.creg3.inside.AD9361_TXnRX = 1;

    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 12, ppos.creg3.word);


	ppos.creg5.inside.slot_dur = 2000;

    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 20, ppos.creg5.word);








    spi_engine_ad9361_init(&spi_eng_ad9361);
    //spi_engine_npu_com_board_init(&spi_eng_npu_com_board);
    //spi_engine_npu_pow_board_init(&spi_eng_npu_pow_board);


    XTime_GetTime(&cur_time);



    //uint8_t d[] = {0x80,0x00,0x81,0x00};
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

    //usleep(50);

    //d[0]=0x80;d[1]=0x00;d[2]=0x00;
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

    uint8_t d[] = {0x84,0x02,0xc0,0x00};

    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
    //d[0]=0x84;d[1]=0x08;d[2]=0x00;
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);



















    ad9361_init(&ad9361_phy, &default_init_param);

	ad9361_set_tx_fir_config(ad9361_phy, tx_fir_config);
	ad9361_set_rx_fir_config(ad9361_phy, rx_fir_config);

    ad9361_set_rx_fir_en_dis(ad9361_phy, 1);
    ad9361_set_tx_fir_en_dis(ad9361_phy, 1);


    //digital power down
    //uint8_t d[] = {0x80,0x09,0x13,0x00};
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
    //digital power up
    //d[0]=0x80;d[1]=0x09;d[2]=0x17;
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);




    XTime_GetTime(&mem_time);

    double tmp_del=(double) mem_time - (double) cur_time;
    tmp_del=tmp_del/333333333.;
    printf("Init ad9361 delay = %10.10f s \n\r", tmp_del);



   //spi_engine_npu_com_board_init(&spi_eng_npu_com_board);
   //spi_engine_npu_pow_board_init(&spi_eng_npu_pow_board);



  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004418, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004458, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004498, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x000044d8, 0x00000002);


//ad9361

#ifdef IS_ZED_BOARD

  d[0]=0x80; d[1]=0x0a;d[2]=0x12;
  no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

#endif


 d[0]=0x80; d[1]=0xfb ;d[2]=0x48;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

 d[0]=0x81;d[1]=0x10;d[2]=0x00;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);


 d[0]=0x81;d[1]=0x06;d[2]=0x6F;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

 d[0]=0x81;d[1]=0x5C;d[2]=0x72;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

 d[0]=0x81;d[1]=0x07;d[2]=0x1f;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

 d[0]=0x81;d[1]=0x08;d[2]=0x25;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

 d[0]=0x81;d[1]=0x12;d[2]=0x45;
 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);







 //fpga
//#ifdef IS_MASTER
//par[8],par[9],par[10],par[11] = 0x80,0x124f80
//d[0]=0x84;d[1]=0x08;d[2]=0xc0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x09;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0a;d[2]=0x4f;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0b;d[2]=0x80; //d[2]=0xe0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

/*
//par[12],par[13],par[14],par[15] = 0x12c05dc0
d[0]=0x84;d[1]=0x0c;d[2]=0x12; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//d[0]=0x84;d[1]=0x0c;d[2]=0x13;  //48064
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0d;d[2]=0xc0; //  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//d[0]=0x84;d[1]=0x0d;d[2]=0x00;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0e;d[2]=0x5d;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0f;d[2]=0xc0;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
*/


//par[12],par[13],par[14],par[15] = 0x12c05dc0
//d[0]=0x84;d[1]=0x0c;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0d;d[2]=0xc0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0e;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0f;d[2]=0xc0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//#endif


//#ifdef IS_SLAVE
//par[8],par[9],par[10],par[11] = 0x80,0x124f00
//d[0]=0x84;d[1]=0x08;d[2]=0x40;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x09;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0a;d[2]=0x4f;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0b;d[2]=0x80;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);


/*

//par[12],par[13],par[14],par[15] = 0x12c05dc0
d[0]=0x84;d[1]=0x0e;d[2]=0x12;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0f;d[2]=0xc0;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0c;d[2]=0x5d; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//d[0]=0x84;d[1]=0x0c;d[2]=0x5e;  // 24064
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0d;d[2]=0xc0; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//d[0]=0x84;d[1]=0x0d;d[2]=0x00;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
*/





//par[12],par[13],par[14],par[15] = 0x12c05dc0
//d[0]=0x84;d[1]=0x0c;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0d;d[2]=0xc0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0e;d[2]=0x12;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x0f;d[2]=0xc0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);




//#endif



//par[5],par[6],par[20],par[21] = 0x1583d702
//d[0]=0x84;d[1]=0x06;d[2]=0x15;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x05;d[2]=0x83;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x15;d[2]=0xd7;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
//d[0]=0x84;d[1]=0x14;d[2]=0x02;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[22] = 0x0a
//d[0]=0x84;d[1]=0x16;d[2]=0x0a;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[23] = 0x0a
//d[0]=0x84;d[1]=0x17;d[2]=0x0a;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);


//pax[2] = 0x99
//d[0]=0x84;d[1]=0x02;d[2]=0xd9;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[16],par[17],par[18],par[19] = 0x07d01194 ( txnrx_setu = 2000, txxnrx_hold = 4500 )

//d[0]=0x84;d[1]=0x10;d[2]=0x07;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//d[0]=0x84;d[1]=0x11;d[2]=0xd0;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//d[0]=0x84;d[1]=0x12;d[2]=0x11;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//d[0]=0x84;d[1]=0x13;d[2]=0x94;
//no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);




/*


for (uint32_t i=0;i<8;i++){

	uint64_t lo_freq_hz = (3500 - 27*(7-i))*1000000;

	ad9361_set_tx_lo_freq(ad9361_phy, lo_freq_hz );
	sleep(0.5);
	ad9361_fastlock_store(ad9361_phy, 0x1, i);
	sleep(0.15);


	ad9361_set_rx_lo_freq(ad9361_phy, lo_freq_hz );
	sleep(0.5);
	ad9361_fastlock_store(ad9361_phy, 0x0, i);
	sleep(0.15);

}

*/




//uint32_t	device_id;
/** maximum transfer speed */
//uint32_t	max_speed_hz;
/** SPI chip select */
//uint8_t		chip_select;
/** SPI mode */
//enum no_os_spi_mode	mode;
/** SPI bit order */
//enum no_os_spi_bit_order	bit_order;
//const struct no_os_spi_platform_ops *platform_ops;
//struct no_os_platform_spi_delays platform_delays;
/**  SPI extra parameters (device specific) */
//void		*extra;
/** Parent of the device */
//struct no_os_spi_desc *parent;

//XGpioPs *Gpio;



//int32_t spi_engine_init(struct no_os_spi_desc **desc,
//			const struct no_os_spi_init_param *param)













modem_init();









//XGpioPs_WritePin(&Gpio_Ps, 46, 1);

//platform_enable_interrupts();


    /* Сид рантайм-конфига из IS_MASTER/IS_SLAVE (Фаза 1 унификации).
     * Должно быть до net_init() и до использования RemoteAddr. */
    config_init_defaults_from_build();

    IP4_ADDR(&RemoteAddr, current_config.peer_ip[0], current_config.peer_ip[1],
                          current_config.peer_ip[2], current_config.peer_ip[3]);

firewall_init();
lwip_init();

#ifdef BRIDGE
    net_init();
#endif

#ifndef BRIDGE
	struct ip4_addr ipaddr, netmask, gw;
	/* the mac address of the board*/
	unsigned char mac_ethernet_address[] = { 0x00, 0x0a, 0x35, 0x00, 0x01, 0x10 };
    echo_netif = &server_netif;

    IP4_ADDR(&ipaddr, 169, 254, 206, 11);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 169, 254, 206, 1);



    if (!xemac_add(echo_netif, &ipaddr, &netmask, &gw, mac_ethernet_address, PLATFORM_EMAC_BASEADDR)) {
        xil_printf("Error adding N/W interface\n\r");
        return -1;
    }

    netif_set_default(echo_netif);

    netif_set_up(echo_netif);

#endif






    status_pcb = udp_new(); // send status & debug pkts
    udp_bind_netif (status_pcb, &bridge_netif); //&udp_netif2);
    status = udp_bind(status_pcb,  &bridge_netif.ip_addr, StatusPort);


    cmd_udp_pcb = udp_new();
    ip_set_option(cmd_udp_pcb, SOF_BROADCAST);

    //udp_bind_netif(cmd_udp_pcb, &bridge_netif);
    udp_bind(cmd_udp_pcb, IP_ADDR_ANY, CmdPort);
    udp_recv(cmd_udp_pcb, cmd_udp_callback, NULL);
    xil_printf("[INIT] cmd_udp_pcb bound to port %d on bridge_netif\r\n", CmdPort);






    //send_pcb = udp_new();
    recv_pcb = udp_new();
    udp_bind_netif (recv_pcb, &bridge_netif); //&udp_netif2);
    status = udp_bind(recv_pcb,  &bridge_netif.ip_addr, 5008);
    //status = udp_bind(recv_pcb, &RemoteAddr, 5008);

    udp_recv(recv_pcb, recv_callback, NULL);


    send_rx_data = udp_new(); // temp send video pkts
    udp_bind_netif (send_rx_data, &bridge_netif); //&udp_netif2);



    debug_pcb = udp_new(); // send status & debug pkts
    udp_bind_netif (debug_pcb, &bridge_netif); //&udp_netif2);


    //udp_recv(debug_pcb, cmd_udp_callback, NULL);



    for (int i=0;i<FIFO_RX_DEPTH;i++) fifo_rx_buf[i].rx_is_val=0;




    platform_enable_interrupts();
    is_run = true;

    /* FW-баннер (версия/роль) — UART уже поднят. */
    xil_printf("\r\n[FW] %s\r\n", FW_BUILD_STR);

    /* Watchdog запускаем последним — все init'ы выполнены. GIC инициализирован
     * в platform_setup_interrupts. Старт по WDT_ENABLED_DEFAULT; тумблер 0x90 0x4C. */
    if (wdt_init((struct XScuGic *)&ScuGic) == 0) {
        if (WDT_ENABLED_DEFAULT) wdt_start();
    }

    while(1)
    {
    	wdt_hb_main();
    	wdt_section_mark(WDT_SEC_OTHER);   /* по умолчанию — переопределяется ниже */


    	XTime_GetTime(&tmp_time);


    	//uint32_t fifo_rx_isr = AXI_CNTRL_REG_READ(FIFO_RX_DEV_BASE_ADDR + FIFO_ISR_OFFSET, 0);

    	//uint32_t fifo_rx_isr = Xil_In32(FIFO_RX_DEV_BASE_ADDR + XLLF_ISR_OFFSET);
        //if ( (fifo_rx_isr&0x00100000) == 0x00100000)
        //{

        //	printf("FIFO ERROR !!!!!!!!!!! \n\r");

        	//Xil_Out32(FIFO_RX_DEV_BASE_ADDR + XLLF_RDFR_OFFSET, 0xA5); // Сброс RX FIFO [PG080]
        	//Xil_Out32(FIFO_RX_DEV_BASE_ADDR + XLLF_ISR_OFFSET, 0xFFFFFFFF); // Очистка статусов [PG080]

        //}


    if ( rx_chan0.creg0.inside.pkt_len == 4800 )
            {
    		  fifo_rx_buf_write(&fifo_rx_buf[0],&fifo_rx_wad, &cmd_rx_buf[0]);
    		}

    if ( rx_chan0.creg0.inside.pkt_len == 576  ) drain_rx_fifo();






   	if ( tx_chan0.creg0.inside.pkt_len == 4800 )
    		{
    		 cmd_buf_exch(buf_cmd, cmd_out);
    		 send_cmd();
    		}

    	if ( tx_chan0.creg0.inside.pkt_len == 576  )
    	   {
    		 tx_process();
    		 radio_tx_periodic_flush();
    	   }











  //if (rx_seg_on)
 // {


	 //cmd_buf_exch(buf_cmd, cmd_out);
	 //radio_tx_periodic_flush();
     //radio_poll();

     //radio_stream_print_stats();

//	  drain_rx_fifo();

 // }
 // else
 // {
   // send_cmd();

   //vid_buf_exch(buf_vid, vid_out);
   //cmd_buf_exch(buf_cmd, cmd_out);
   //fifo_rx_buf_write(&fifo_rx_buf[0],&fifo_rx_wad, &cmd_rx_buf[0]);

   //#ifdef IS_MASTER
    //fifo_rx_buf_check(&fifo_rx_buf[0],&fifo_rx_wad);
   //#endif

 // }


//    	counter_err++;
//
//    	if (counter_err >= 64000) {
//    		counter_err = 0;
//
//    		xil_printf("------------------Debug information------------------------\n\r"
//    	    				   "Ethernet send: %llu (%llu)\n\r"
//    	    				   "Ethernet read: %llu\n\r"
//    	    				   "Buffer write : %llu\n\r"
//    	    				   "Buffer Read  : %llu (%llu)\n\r"
//    	    				   "Buffer Formed: %llu (%llu) (%llu/5)\n\r"
//    	    				   "FIFO send    : %llu (%llu)\n\r"
//    						   "FIFO read    : %llu (%llu, Bad: %llu)\n\r"
//    	          	   "-----------------------------------------------------------\n\r",
//    						   val_send_eth * 5, val_send_eth,
//							   val_recv_eth,
//    						   val_buff_write,
//							   val_buff_read * 5,
//							   val_buff_read,
//							   val_group_buf * 5, val_group_buf, err_last_packet(),
//    						   val_send_fifo * 5, val_send_fifo,
//							   (val_recv_fifo + val_recv_fifo_wrong) * 5, val_recv_fifo, val_recv_fifo_wrong);
//  }


    	//vid_buf_exch(buf_vid, vid_out);
    	//cmd_buf_exch(buf_cmd, cmd_out);

    	//fifo_rx_buf_write(&fifo_rx_buf[0],&fifo_rx_wad, &cmd_rx_buf[0]);

//#ifdef IS_MASTER

    	//fifo_rx_buf_check(&fifo_rx_buf[0],&fifo_rx_wad);
//#endif

    	cnt_uart_rx_buf=uart_cmd_chan(cnt_uart_rx_buf,&buf, &Uart1_Ps, &desc_tmp, &func_start, &func_type, &func_par[0], &fpga_regs[0]);
    	//tx_seg_on = ((fpga_regs[12]==0x02)&&(fpga_regs[13]==0x40));
    	//rx_seg_on = ((fpga_regs[14]==0x02)&&(fpga_regs[15]==0x40));
    	//debug_ram_load(&bufff_cnt, &bufff[0], &Gpio_Ps, &Uart1_Ps);
    	wdt_section_mark(WDT_SEC_EXEC_FUNC);
    	exec_func(&func_start, func_type, &func_par[0]);

#ifdef BRIDGE

         //u8 c =  XUartPs_RecvByte(Uart1_Ps.Config.BaseAddress);
         //if (c == 'D' || c == 'd') {
           //  print_dynamic_fdb(&bridge_netif);
         //}

         //else if (c == 'S' || c == 's') {
         //    char tmp [5]  = {0x01, 0x02, 0x03, 0x04, 0x04};
         //    udp2_send_to_pc(&tmp[0], 5);
         //}
#endif

    	wdt_section_mark(WDT_SEC_LWIP_INPUT);
    	xemacif_input(echo_netif);
    	wdt_section_mark(WDT_SEC_POST_LWIP);
    	wdt_hb_lwip();

        // Обработка таймеров (ARP, TCP, DHCP)
        //sys_check_timeouts();


    	wdt_section_mark(WDT_SEC_FIFO_DEB);
    	fifo_deb_read();
    	wdt_section_mark(WDT_SEC_OTHER);




    	if (is_pow_rx)
    		{


    		  uint8_t d[] = {0x01,0x61,0x00,0x00};
    		  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		  //xil_printf("%02X ", d[2]);

    		  float rssi = -d[2];
    		  rssi/=4.;

    		  d[0] = 0x02; d[1] = 0xb0;
    		  no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		  //xil_printf("%02X", d[2]);

    		  rssi-=d[2];

    		  gain_index_rx = d[2];

    		  pow_rx = rssi + 13.9397;//3500 tone corection



    		  ppos.creg7.inside.del_ack = Xil_In32(AXI_CNTRL_REG_BASEADDR + 7*4);










    		if (status_modem->status_rx.cnt_pkt == 0)
    	    		{
    	    			status_modem->status_rx.max_pow = pow_rx;
    	    			status_modem->status_rx.min_pow = pow_rx;
    					status_modem->status_rx.acc_pow = pow_rx;


    					status_modem->status_rx.max_gain_index = gain_index_rx;
    					status_modem->status_rx.min_gain_index = gain_index_rx;

    					mean_dist = ppos.creg7.inside.del_ack;

    	    		}
    	    		else
    	    		{
    	    			if (status_modem->status_rx.max_pow < pow_rx) status_modem->status_rx.max_pow = pow_rx;
    	    			if (status_modem->status_rx.min_pow > pow_rx) status_modem->status_rx.min_pow = pow_rx;
    	    			status_modem->status_rx.acc_pow += pow_rx;


    	    			if (status_modem->status_rx.max_gain_index < gain_index_rx) status_modem->status_rx.max_gain_index = gain_index_rx;
    	    		    if (status_modem->status_rx.min_gain_index > gain_index_rx) status_modem->status_rx.min_gain_index = gain_index_rx;


    	    		    mean_dist = mean_dist +  ppos.creg7.inside.del_ack;
    	    		}


    		status_modem->status_rx.cnt_pkt++;


    		is_pow_rx = false;
    		}







/*


    	if (is_pow_cntrl_tx)
    		{

    		if (status_modem->status_cntrl_tx.cnt_pkt == 0)
    		{
    			status_modem->status_cntrl_tx.max_pow_cntrl = pow_cntrl_tx;
    			status_modem->status_cntrl_tx.min_pow_cntrl = pow_cntrl_tx;
				status_modem->status_cntrl_tx.acc_pow_cntrl = pow_cntrl_tx;
    		}
    		else
    		{
    			if (status_modem->status_cntrl_tx.max_pow_cntrl < pow_cntrl_tx) status_modem->status_cntrl_tx.max_pow_cntrl = pow_cntrl_tx;
    			if (status_modem->status_cntrl_tx.min_pow_cntrl > pow_cntrl_tx) status_modem->status_cntrl_tx.min_pow_cntrl = pow_cntrl_tx;
    			status_modem->status_cntrl_tx.acc_pow_cntrl += pow_cntrl_tx;
    		}


    		is_pow_cntrl_tx = false;
    		}


*/



/*


    if (is_cntrl_pkt_end)
    {
    	//uint8_t dtmpp[] = {0x04,0x25,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    	//no_os_spi_write_and_read_11bit(&desc_tmp,&dtmpp[0],10);


    	uint32_t dat = Xil_In32(RX_CORE_BASEADDR + 0x000000b8);

    	bool is_check = dat&0x00000001;
    	uint16_t count = dat>>16;

    	bool gap_pkt;
        if (count == 0) gap_pkt =   status_modem->status_cntrl_tx.last_pkt == 0xffff;
        else gap_pkt =   status_modem->status_cntrl_tx.last_pkt == count-1;

        status_modem->status_cntrl_tx.last_pkt = count;

        if (!(is_check&gap_pkt))
		{
        	status_modem->status_cntrl_tx.cnt_err_pkt++;


        	// printf("%08X %08X \n\r", status_modem->status_cntrl_tx.last_dat, dat);



		}


        status_modem->status_cntrl_tx.last_dat = dat;

        status_modem->status_cntrl_tx.cnt_pkt++;


        //printf("%08X \n\r", dat);

    	 // Xil_Out32(XPAR_BRAM_0_BASEADDR + 0, 0x2);

		// status_modem->status_cntrl_tx->cnt_pkt = 0;
		// status_modem->status_cntrl_tx->cnt_err_pkt = 0;


   	    //printf("c_pkt =");
   	  //  for (int k = 0; k<10; k++)
   	  //  {
   	   	  // printf("%02X", d[2+k]);
   	  //  	XUartPs_SendByte(Uart1_Ps.Config.BaseAddress,dtmpp[k+2]);
      //  }

   	    //printf("\n\r");

    	is_cntrl_pkt_end = false;

    	    	 }


*/





/*

    	 XTime_GetTime(&cur_time);


    	 if (cur_time - mem_time >= XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ )
    	    	 {
    		         print_dynamic_fdb(&bridge_netif);




    		 printf("DEB CNT: %d  %d \n\r",  deb_cnt_br,  deb_cnt_rf);



    		 mem_time=cur_time;
    	    	 }

*/


    	XTime_GetTime(&cur_time);


    	 if (cur_time - mem_time >= COUNTS_PER_SECOND )


    	 {
    		 mem_time=cur_time;

    		 if (led_tog)
    		 {
    			 XGpioPs_WritePin(&Gpio_Ps, 14, 0);
    			 XGpioPs_WritePin(&Gpio_Ps, 15, 0);
    		 }
    		 else
    		 {
    			 XGpioPs_WritePin(&Gpio_Ps, 14, 1);
    		     XGpioPs_WritePin(&Gpio_Ps, 15, 1);
    		 }

    		 led_tog = !led_tog;

    		 int32_t t;
    		 ad9361_get_temperature(ad9361_phy, &t);
    		 float tf = 0;

    		 tf = t;
    		 tf=tf/1000.;

    		 status_modem->TempAD9361 = tf;

    		 //printf("ad9361 temp = %.1f \n\r", tf);
    		 //xil_printf("ad9361 temp =  %d\n\r", t);

    	     u32 TempRawData;
    		 float TempData;

    		 TempRawData = XSysMon_GetAdcData(XADCInstPtr, XSM_CH_TEMP);
    		 TempData    = XSysMon_RawToTemperature(TempRawData);
    		//xil_printf("Zynq Raw Temp %lu \n\r", TempRawData);
    		//printf("Zynq Temp = %.1f \n\r", TempData);

    		 status_modem->TempFPGA = TempData;




    		 /*


    		 uint8_t d[] = {0x01,0x63,0x00,0x00};
    		 no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		 float pps_pow_cntrl = -d[2];
    		 pps_pow_cntrl/=4.;
    		 d[0] = 0x02; d[1] = 0xb5;
    		 no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		 pps_pow_cntrl-=d[2];

    		 status_modem->pps_pow_cntrl = pps_pow_cntrl;





    		 d[0] = 0x01; d[1] = 0x61;
    		 no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		 float pps_pow = -d[2];
    		 pps_pow/=4.;
    		 d[0] = 0x02; d[1] = 0xb0;
    		 no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		 pps_pow-=d[2];

    		 status_modem->pps_pow = pps_pow;

    		 d[0] = 0x04; d[1] = 0x29;
    		 no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
    	     status_modem->gpio_status = d[2];

*/




    		 //temp
/*
    		  uint8_t d[] = {0x01,0x61,0x00,0x00};
    		  no_os_spi_write_and_read(&desc_tmp,&d[0],1);


    		   float rssi = -d[2];
    		   rssi/=4.;

    		   d[0] = 0x02; d[1] = 0xb0;
    		   no_os_spi_write_and_read(&desc_tmp,&d[0],1);
    		   //xil_printf("%02X", d[2]);

    		   rssi-=d[2];

    		   gain_index_rx = d[2];

    		   pow_rx = rssi + 13.9397;



    		   status_modem->status_rx.max_pow = pow_rx;

    	       status_modem->status_rx.max_gain_index = gain_index_rx;

*/
    		 //








    			 //

    	    	      //mean_dist = 100.*mean_dist;
    	              //mean_dist = mean_dist / status_modem->status_rx.cnt_pkt;

    	      	     status_modem->status_npu.del_ack = mean_dist;



    	      	  // status_modem->creg2 = Xil_In32(AXI_CNTRL_REG_BASEADDR + 4*2);




    	     wdt_section_mark(WDT_SEC_STATUS_SEND);
    	     send_modem_status(status_pcb, status_modem);




    		 //float pps_pow;
    		 //uint8_t gpio_status;


    	     //u32 PLVRawData;
    		 //float PLVData;

    		 //PLVRawData = XSysMon_GetAdcData(XADCInstPtr, XSM_CH_VCCINT);
    		 //PLVData    = XSysMon_RawToVoltage(PLVRawData);
    		 //printf("Zynq VCCINT = %.2f \n\r", PLVData);

    		 //PLVRawData = XSysMon_GetAdcData(XADCInstPtr, XSM_CH_VCCAUX);
    	     //PLVData    = XSysMon_RawToVoltage(PLVRawData);
    	     //printf("Zynq VCCAUX = %.2f \n\r", PLVData);

    		 //PLVRawData = XSysMon_GetAdcData(XADCInstPtr, XSM_CH_VBRAM);
    	     //PLVData    = XSysMon_RawToVoltage(PLVRawData);
    	     //printf("Zynq VBRAM = %.2f \n\r", PLVData);



    		 //printf("Ta = %.1f  Tf = %.1f  cnt_pkt_cntrl= %d  cnt_err_pkt_cntrl = %d min_pow_cntrl =  %.1f max_pow_cntrl = %.1f acc_pow_cntrl = %.1f  min_pow =  %.1f max_pow = %.1f acc_pow = %.1f  cnt_pkt= %d  pps_pow_cntrl = %.1f  \n\r",
    		 //status_modem->TempAD9361, status_modem->TempFPGA , status_modem->status_cntrl_tx->cnt_pkt, status_modem->status_cntrl_tx->cnt_err_pkt,
			 //status_modem->status_cntrl_tx->min_pow_cntrl, status_modem->status_cntrl_tx->max_pow_cntrl,status_modem->status_cntrl_tx->acc_pow_cntrl,
			 //status_modem->status_rx->min_pow,             status_modem->status_rx->max_pow,     status_modem->status_rx->acc_pow,
			 //status_modem->status_rx->cnt_pkt, pps_pow_cntrl);

    		 //status_modem->status_cntrl_tx.cnt_pkt = 0;
    		 //status_modem->status_cntrl_tx.cnt_err_pkt = 0;

    		 status_modem->status_rx.cnt_pkt = 0;
    		 //status_modem->status_rx.cnt_err_pkt = 0;

    	 }




    	 XTime_GetTime(&cur_time);


    	 tmp_time = cur_time - tmp_time;

    	 if (tmp_time > max_tmp_time)

    	 {
    		 max_tmp_time = tmp_time;
    		 printf("%llu \n\r", tmp_time);
    	 }




    	wdt_kick();
    }

    cleanup_platform();
    return 0;
}





void fifo_deb_read()
{
if(XLlFifo_iRxOccupancy(&fifo_deb)) {
/* Read Receive Length */
u32 ReceiveLength = (XLlFifo_iRxGetLen(&fifo_deb))/4;
u32 RxWord;
bool is_val = false;
if (ReceiveLength == 256) is_val = true;

for (int i=0; i < ReceiveLength; i++) {
RxWord = XLlFifo_RxGetWord(&fifo_deb);
if (is_val)
	{
	 deb_pkt_buf[deb_pkt_w] = RxWord;
	 deb_pkt_w=(deb_pkt_w == 4095)?0:deb_pkt_w+1;
	}
}


if (!is_val) xil_printf("! debug wrong: ReceiveLength =  %08x\n\r", ReceiveLength);


if (is_val && current_config.role == ROLE_MASTER) send_deb();





}


}




void send_deb() {


	//udp_bind_netif (debug_pcb, &bridge_netif); //&udp_netif2);

	//udp_bind_netif (status_pcb, &bridge_netif); //&udp_netif2);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 1024, PBUF_RAM);
    if (!p) {
        xil_printf("Failed to allocate pbuf for deb packet\n\r");
        return;
    }

    memcpy((uint8_t*) p->payload, (uint8_t*) &deb_pkt_buf[deb_pkt_r], 1024);
    deb_pkt_r=(deb_pkt_r + 256  == 4096)?0:deb_pkt_r + 256;





    //udp_disconnect(debug_pcb);
    //err_t connect_err = udp_connect(debug_pcb, &RemoteAddr, RemotePortDeb);
    //if (connect_err != ERR_OK) {
    //    xil_printf("Error connecting send_deb: %d ,deb_pkt_r = %d\n\r", connect_err,  deb_pkt_r);
    //    pbuf_free(p);
    //    return;
    //}

    //err_t out = udp_sendto(debug_pcb, p, &RemoteAddr, RemotePortDeb);

    err_t out = udp_sendto(status_pcb, p, &RemoteAddr, RemotePortDeb);



    if (out != ERR_OK) {
        xil_printf("Error udp_sendto debug_pcb: %d\n\r", out);
    }


    pbuf_free(p);
    //xil_printf("Reset packet sent\n\r");
}


/*

void send_modem_status(struct stat_modem* status) {


	udp_bind_netif (debug_pcb, &bridge_netif); //&udp_netif2);

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




















void fifo_rx_buf_write(struct vid_pkt* rx_buf, uint8_t* rx_wad, uint8_t* cmd_rx_buf)
{
	bool is_val;
	if(XLlFifo_iRxOccupancy(&fifo)) {
		u32 RxWord;
		u32 preamb0, preamb1;
		u32 ReceiveLength = (XLlFifo_iRxGetLen(&fifo))/4;

		//err_inc_recv_fifo();

		if (ReceiveLength==2250)
		{


			// xil_printf("CMD_REC -> 2250\n\r");

			RxWord = XLlFifo_RxGetWord(&fifo);
			preamb0 = RxWord;
			RxWord = XLlFifo_RxGetWord(&fifo);
			preamb1 = RxWord;

			is_val = (preamb0 == 0xb6ce261d);
			is_val = is_val && ((preamb1&0xffff0000) == 0x6d9c0000);




			if (is_val)
			{
				preamb0 = swap32(preamb0);
				preamb1 = swap32(preamb1);
				memcpy((uint8_t*) &rx_buf[*rx_wad],  &preamb0,4);
				memcpy((uint8_t*) &rx_buf[*rx_wad]+4,&preamb1,4);



			} else {
				//xil_printf("!is_pream %02X:%02X:%02X:%02X:%02X:%02X \n\r", (preamb0>>24)&0x000000ff,(preamb0>>16)&0x000000ff,(preamb0>>8)&0x000000ff,
				//		preamb0&0x000000ff, (preamb1>>24)&0x000000ff,(preamb1>>16)&0x000000ff );


			}


			 u32 sum = 0;
            // u32 w_cnt_p = 0;


			 sum+=preamb0;
			 sum+=preamb1;

		    for (int i=2; i < ReceiveLength; i++) {
				RxWord = XLlFifo_RxGetWord(&fifo);
				RxWord = swap32(RxWord);


				if (i < ReceiveLength-1) sum+=RxWord;


				//if (i == 41) w_cnt_p = RxWord;

				if (is_val) memcpy((u32*) &rx_buf[*rx_wad] + i, &RxWord, 4);
		    }








		    if (!is_val) xil_printf("!is_val");

		    if (RxWord != sum)	xil_printf("!sum");

            u16 cnt_pkt = (preamb1>>16)&0x0000ffff;

            bool alarm = false;
            if (cnt_pkt == 0)	alarm = deb_cnt_pkt!=0xffff;
            else alarm  = cnt_pkt-1 != deb_cnt_pkt;

           if (alarm)  xil_printf("%04X\n\r",cnt_pkt);

		    deb_cnt_pkt = cnt_pkt;





		    if (!is_val) return;


//			uint16_t size[5] = {1269, 1269, 1269, 1269, 1269};
//			memcpy((uint8_t*)size + 0, (uint8_t*)(rx_buf[*rx_wad].head) + 80, sizeof(uint16_t));
//			memcpy((uint8_t*)size + 2, (uint8_t*)(rx_buf[*rx_wad].head) + 82, sizeof(uint16_t));
//			memcpy((uint8_t*)size + 4, (uint8_t*)(rx_buf[*rx_wad].head) + 84, sizeof(uint16_t));
//			memcpy((uint8_t*)size + 6, (uint8_t*)(rx_buf[*rx_wad].head) + 86, sizeof(uint16_t));
//			memcpy((uint8_t*)size + 8, (uint8_t*)(rx_buf[*rx_wad].head) + 88, sizeof(uint16_t));
//
//			//if (size[0] != 0) {
//			send_vid_packet_sized(rx_buf[*rx_wad].vid_0, size[0]);
//			send_vid_packet_sized(rx_buf[*rx_wad].vid_1, size[1]);
//			send_vid_packet_sized(rx_buf[*rx_wad].vid_2, size[2]);
//			send_vid_packet_sized(rx_buf[*rx_wad].vid_3, size[3]);
//			send_vid_packet_sized(rx_buf[*rx_wad].vid_4, size[4]);



			 uint8_t check = 0;
			 uint16_t size;
			 memcpy(&size,  rx_buf[*rx_wad].head+100, sizeof(uint16_t) );
			 memcpy(&check, rx_buf[*rx_wad].head+102  ,sizeof(uint8_t) );

			 if ((size != 0)&&(size<=1518))
				 {
				 	   uint8_t check_solve = 0;
			           for (int k=0; k<size; k++) check_solve+= rx_buf[*rx_wad].tel[k];
				      // xil_printf("CMD_REC -> len = %d, check = %d check_solve = %d\n\r", size, check, check_solve);

				       if (check == check_solve)
				       {
				        struct pbuf *p;
				        p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
				        memcpy((uint8_t*) p->payload, rx_buf[*rx_wad].tel , size);


						if ( port3_bridge.input(p, &port3_bridge) != ERR_OK) {
									pbuf_free(p);
									p = NULL;
								}

						//port3_bridge.input(p, &port3_bridge);
				        //pbuf_free(p);

				       }
				 }




			rx_buf[*rx_wad].rx_is_val = is_val;
			if (is_val) *rx_wad=(*rx_wad == FIFO_RX_DEPTH - 1) ? 0 : *rx_wad + 1;


			return;

		}

		else if (ReceiveLength==450)
		{


			RxWord = XLlFifo_RxGetWord(&fifo);
			preamb0 = RxWord;
			RxWord = XLlFifo_RxGetWord(&fifo);
			preamb1 = RxWord;

			memcpy((uint8_t*) &cmd_rx_buf[0],  &preamb0,4);
			memcpy((uint8_t*) &cmd_rx_buf[0]+4,&preamb1,4);

			is_val = (preamb0 == 0xb6ce261d);
			is_val = is_val && ((preamb1&0xffff0000) == 0x6d9c0000);


			for (int i=2; i < ReceiveLength; i++) {
							RxWord = XLlFifo_RxGetWord(&fifo);
							RxWord = swap32(RxWord);
							memcpy((u32*) &cmd_rx_buf[0] + i, &RxWord, 4);
						}


					if (!is_val)
			{

						xil_printf("!is_val %08x %08x \n\r",preamb0,preamb1);
			            return;
			}



			 uint8_t check = 0;
			 uint16_t size;
			 memcpy(&size,  &cmd_rx_buf[0]+20  ,sizeof(uint16_t) );
			 memcpy(&check, &cmd_rx_buf[0]+22  ,sizeof(uint8_t) );

			 if ((size != 0)&&(size<=1518))
				 {
				 	   uint8_t check_solve = 0;
			           bool check_empty = false;
				 	   for (int k=0; k<size; k++)
			        	   {
				 		     check_empty = check_empty || cmd_rx_buf[29+k] == 0;
			        	     check_solve+= cmd_rx_buf[29+k];
			        	   }
				       //xil_printf("CMD_REC -> len = %d, check = %d check_solve = %d\n\r", size, check, check_solve);


				       //if (!check_empty&&(check == check_solve))
				    if (check == check_solve)
				       {
				        struct pbuf *p;
				       // p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);

				        p = pbuf_alloc(PBUF_RAW, size, PBUF_RAM);

				        //if (size == 1514)
				        //{
				        //	printf("! %d\n\r",p->len);
				        //}



				        memcpy((uint8_t*) p->payload, &cmd_rx_buf[29], size);


				         //  struct eth_hdr *ethhdr = (struct eth_hdr*) p->payload;
					     //  printf("main-> port3_bridge.input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d \n\r",
					     // (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
					     // (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
					     // (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
					     // (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
					     // lwip_htons(ethhdr->type), p->tot_len  );




						if ( port3_bridge.input(p, &port3_bridge) != ERR_OK) {
													pbuf_free(p);
													p = NULL;
												}





				        //port3_bridge.input(p, &port3_bridge);
						//pbuf_free(p);



				       }
				 }

return;

		}




		else if (ReceiveLength==54)
				{


					RxWord = XLlFifo_RxGetWord(&fifo);
					preamb0 = RxWord;
					RxWord = XLlFifo_RxGetWord(&fifo);
					preamb1 = RxWord;

					memcpy((uint8_t*) &cmd_rx_buf[0],  &preamb0,4);
					memcpy((uint8_t*) &cmd_rx_buf[0]+4,&preamb1,4);

					is_val = (preamb0 == 0xb6ce261d);
					is_val = is_val && ((preamb1&0xffff0000) == 0x6d9c0000);


					for (int i=2; i < ReceiveLength; i++) {
									RxWord = XLlFifo_RxGetWord(&fifo);
									RxWord = swap32(RxWord);
									//memcpy((u32*) &cmd_rx_buf[0] + i, &RxWord, 4);
								}


							if (!is_val)
					{

								xil_printf("!p54 is_val %08x %08x \n\r",preamb0,preamb1);
					            return;
					}


/*
					 uint8_t check = 0;
					 uint16_t size;
					 memcpy(&size,  &cmd_rx_buf[0]+20  ,sizeof(uint16_t) );
					 memcpy(&check, &cmd_rx_buf[0]+22  ,sizeof(uint8_t) );

					 if ((size != 0)&&(size<=1518))
						 {
						 	   uint8_t check_solve = 0;
					           bool check_empty = false;
						 	   for (int k=0; k<size; k++)
					        	   {
						 		     check_empty = check_empty || cmd_rx_buf[29+k] == 0;
					        	     check_solve+= cmd_rx_buf[29+k];
					        	   }
						       //xil_printf("CMD_REC -> len = %d, check = %d check_solve = %d\n\r", size, check, check_solve);


						       //if (!check_empty&&(check == check_solve))
						    if (check == check_solve)
						       {
						        struct pbuf *p;
						        p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
						        memcpy((uint8_t*) p->payload, &cmd_rx_buf[29], size);


						         //  struct eth_hdr *ethhdr = (struct eth_hdr*) p->payload;
							     //  printf("main-> port3_bridge.input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d \n\r",
							     // (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
							     // (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
							     // (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
							     // (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
							     // lwip_htons(ethhdr->type), p->tot_len  );




								if ( port3_bridge.input(p, &port3_bridge) != ERR_OK) {
															pbuf_free(p);
															p = NULL;
														}





						        //port3_bridge.input(p, &port3_bridge);
								//pbuf_free(p);



						       }
						 }

						 */

		return;

				}
































		else




		{
			//err_inc_recv_fifo_wrong();


			//xil_printf("! data wrong: ReceiveLength =  %08x\n\r", ReceiveLength);
			xil_printf("%d\n\r", ReceiveLength);

			for (int i=0; i < ReceiveLength; i++) {
				RxWord = XLlFifo_RxGetWord(&fifo);


			}
		}





	}
}


/*
void fifo_rx_buf_write(struct krt_gr_dat* rx_buf, uint8_t* rx_wad)
{
bool is_val;
if(XLlFifo_iRxOccupancy(&fifo)) {
u32 RxWord;
u32 preamb0, preamb1;

u32 ReceiveLength = (XLlFifo_iRxGetLen(&fifo))/4;


if (ReceiveLength==2250)
{
	RxWord = XLlFifo_RxGetWord(&fifo);
	//RxWord = swap32(RxWord);
    preamb0 = RxWord;

	//xil_printf("%08x\n\r",RxWord);

    RxWord = XLlFifo_RxGetWord(&fifo);
    //RxWord = swap32(RxWord);
    preamb1 = RxWord;

    //xil_printf("%08x\n\r",RxWord);

	is_val = (preamb0 == 0xb6ce261d);
	is_val = is_val && ((preamb1&0xffff0000) == 0x6d9c0000);

	if (is_val)
	{
	  //memcpy((u32*) &rx_buf[*rx_wad],  &preamb0,4);
	  //memcpy((u32*) &rx_buf[*rx_wad]+1,&preamb1,4);
	  preamb0 = swap32(preamb0);
	  preamb1 = swap32(preamb1);
	  memcpy((uint8_t*) &rx_buf[*rx_wad],  &preamb0,4);
	  memcpy((uint8_t*) &rx_buf[*rx_wad]+4,&preamb1,4);
	}

    for (int i=2; i < ReceiveLength; i++) {
    RxWord = XLlFifo_RxGetWord(&fifo);
    RxWord = swap32(RxWord);
    //if (is_val) memcpy((u32*) &rx_buf[*rx_wad]+i,&RxWord,4);
    if (is_val) memcpy((uint8_t*) &rx_buf[*rx_wad]+4*i,&RxWord,4);
    }

  rx_buf[*rx_wad].rx_is_val=is_val;
  if (is_val) *rx_wad=(*rx_wad==FIFO_RX_DEPTH-1)?0:*rx_wad+1;


}
else
{
	for (int i=0; i < ReceiveLength; i++) {
	RxWord = XLlFifo_RxGetWord(&fifo);
	}

}

}
}
*/

void fifo_rx_buf_check(struct vid_pkt* rx_buf, uint8_t* rx_wad)
{
    uint8_t index;

	//for (uint8_t i=0+5;i<FIFO_RX_DEPTH+5;i++)
	//{
	//	index = (i+*rx_wad>FIFO_RX_DEPTH-1)?i+*rx_wad-FIFO_RX_DEPTH:i+*rx_wad;





	index = fifo_rx_rad;


		if (rx_buf[index].rx_is_val == 1)
			{

			//  uint8_t check=0;
			//  for (int i=0;i<7353; i++) check+=*((uint8_t*)&rx_buf[index].tel+i);

 			  //if (!(rx_buf[index].head[16] == check)) {
 			//	  send_debug_rx_check((uint8_t) rx_buf[index].head[16], check);
 			 // }



 			  uint16_t size[5] = {0, 0, 0, 0, 0};
 			  memcpy((uint8_t*)size + 0, rx_buf[index].head + 80, sizeof(uint16_t));
 			  memcpy((uint8_t*)size + 2, rx_buf[index].head + 82, sizeof(uint16_t));
 			  memcpy((uint8_t*)size + 4, rx_buf[index].head + 84, sizeof(uint16_t));
 			  memcpy((uint8_t*)size + 6, rx_buf[index].head + 86, sizeof(uint16_t));
 			  memcpy((uint8_t*)size + 8, rx_buf[index].head + 88, sizeof(uint16_t));

 			  uint8_t check[5] = {0, 0, 0, 0, 0};
 	 		  memcpy((uint8_t*)check + 0, rx_buf[index].head + 90, sizeof(uint8_t));
 	 		  memcpy((uint8_t*)check + 1, rx_buf[index].head + 91, sizeof(uint8_t));
 	 		  memcpy((uint8_t*)check + 2, rx_buf[index].head + 92, sizeof(uint8_t));
 	 		  memcpy((uint8_t*)check + 3, rx_buf[index].head + 93, sizeof(uint8_t));
 	 		  memcpy((uint8_t*)check + 4, rx_buf[index].head + 94, sizeof(uint8_t));




#ifdef NO_PROTOCOL
		      //tel=rx_buf[index].tel;
		      //send_tel_packet(tel);
#endif

 	 		  uint8_t  rec_check =0;

 	 		  if (size[0]<=1212) { for (int k=0; k<size[0]; k++) rec_check+=rx_buf[index].vid_0[k];
		      if (rec_check == check[0]) send_vid_packet_sized(rx_buf[index].vid_0, size[0]);
		      rec_check =0;}
 	 		if (size[1]<=1212) { for (int k=0; k<size[1]; k++) rec_check+=rx_buf[index].vid_1[k];
		      if (rec_check == check[1]) send_vid_packet_sized(rx_buf[index].vid_1, size[1]);
		      rec_check =0;}
		      if (size[2]<=1212) { for (int k=0; k<size[2]; k++) rec_check+=rx_buf[index].vid_2[k];
		      if (rec_check == check[2]) send_vid_packet_sized(rx_buf[index].vid_2, size[2]);
		      rec_check =0;}
		      if (size[3]<=1212) {   for (int k=0; k<size[3]; k++) rec_check+=rx_buf[index].vid_3[k];
		      if (rec_check == check[3]) send_vid_packet_sized(rx_buf[index].vid_3, size[3]);
		      rec_check =0;}
		      if (size[4]<=1212) { for (int k=0; k<size[4]; k++) rec_check+=rx_buf[index].vid_4[k];
		      if (rec_check == check[4]) send_vid_packet_sized(rx_buf[index].vid_4, size[4]);
		      }
		      rx_buf[index].rx_is_val=0;


//			}
 //break;

		      fifo_rx_rad++;
		      if (fifo_rx_rad == FIFO_RX_DEPTH) fifo_rx_rad = 0;


	}
}






/*
static u32 uart_cmd_chan(u32 cnt_uart_rx_buf,u32 *buf, XUartPs *uart_ptr, struct no_os_spi_desc *spi_desc_ptr,  bool *func_start, u8 *func_type, u8 *func_par, u8 *fpga_reg)
{
	u32 RecvChar;
	uint8_t data[4];
	u32 cnt;
	cnt=cnt_uart_rx_buf;

   	if (XUartPs_IsReceiveData(uart_ptr->Config.BaseAddress))
    	{
    	RecvChar = XUartPs_ReadReg(uart_ptr->Config.BaseAddress,
    					    XUARTPS_FIFO_OFFSET);

    	*buf=*buf<<8;
    	*buf+=(0x000000ff&RecvChar);
    	cnt++;

    	if (cnt==3)
    	{

    	RecvChar=*buf;
    	RecvChar=RecvChar>>19;
    	RecvChar=RecvChar&0x0000000f;

    	if (RecvChar==0)
    	{
    	data[2]=(0x000000ff&*buf);
    	data[1]=(0x000000ff&(*buf>>8));
    	data[0]=(0x000000ff&(*buf>>16));

    	cnt=0;
    	*buf=0;

    	if((data[0]&0x07)<=0x04)
    	{
    	 no_os_spi_write_and_read_11bit(spi_desc_ptr,&data[0],0x1);
    	 if ((data[0]&0x80)==0) XUartPs_SendByte(uart_ptr->Config.BaseAddress,data[2]);

    	// if ((data[0]&0x07)==0x04) fpga_reg[data[1]] = data[2];


    	}

    	if((data[0]&0x07)>0x04)
        {
          if ((data[0]==0x86)&&(data[1]==0x01))  *func_type=data[2];
    	  if ((data[0]==0x86)&&(data[1]>=0x02)&&(data[1]<0x12)) func_par[data[1]-0x02]=data[2]; //16 pars ?
    	  if ((data[0]==0x86)&&(data[1]==0x00))  *func_start=((data[2]&0x01)==0x01);
        }

    	}
    	else cnt--;
    	}
    }

return cnt;
}

*/




static u32 uart_cmd_chan(u32 cnt_uart_rx_buf, u32 *buf, XUartPs *uart_ptr, struct no_os_spi_desc *spi_desc_ptr,  bool *func_start, u8 *func_type, u8 *func_par, u8 *fpga_reg)
{
    u32 RecvChar;
    u32 cnt = cnt_uart_rx_buf;
    uint8_t packet[3];

    if (XUartPs_IsReceiveData(uart_ptr->Config.BaseAddress))
    {
        RecvChar = XUartPs_ReadReg(uart_ptr->Config.BaseAddress, XUARTPS_FIFO_OFFSET);

        *buf = *buf << 8;
        *buf += (0x000000ff & RecvChar);
        cnt++;

        if (cnt == 3)
        {
            u32 check_bits = (*buf >> 19) & 0x0F;

            if (check_bits == 0)
            {
                packet[0] = (uint8_t)(*buf >> 16);
                packet[1] = (uint8_t)(*buf >> 8);
                packet[2] = (uint8_t)(*buf);

                current_cmd_source = CMD_SRC_UART;

                if ((packet[0]&0x07)==0x04) fpga_reg[packet[1]] = packet[2];

                process_cmd_3bytes(packet, uart_ptr, spi_desc_ptr, func_start, func_type, func_par);

                cnt = 0;
                *buf = 0;
            }
            else
            {
                 cnt--;
            }
        }
    }
    return cnt;
}


























static int debug_ram_load(u32 *bufff_cnt, uint8_t *bufff, XGpioPs *gpio_ptr, XUartPs *uart_ptr)
{
	   u32 RecvChar;
	   u32 b;
	   u8 t;

	   if(0x00000001==(XGpioPs_ReadPin(gpio_ptr, 54+24+32)&0x00000001))
	   {
	       usleep(100); //?????????????????????????????????????????????????????????????????????????????????????????????? UART | GPIO ?
	       b=0;
	       for (int i=0;i<8;i++)
	        {
	    	  RecvChar=XGpioPs_ReadPin(gpio_ptr, 54+7-i+32)&0x00000001;
	    	  b+=RecvChar;
	    	  if (i<7) b<<=1;
	    	}

	      XGpioPs_WritePin(gpio_ptr, 54+25, 0);
	      RecvChar=0;
	      while(0x00000001==(XGpioPs_ReadPin(gpio_ptr, 54+24+32)&0x00000001))
	       {
	    	 RecvChar++;
	    	 if (RecvChar==0x00ffffff)
	    	  {
	    	    b=0xff;
	    	    *bufff_cnt=0;
	    	    return  1;
	    	  }
	       }
	    XGpioPs_WritePin(gpio_ptr, 54+25, 1);

        t=(0x000000ff&b);
        XUartPs_SendByte(uart_ptr->Config.BaseAddress, t);

        memcpy(bufff+*bufff_cnt,&t,1);
	   if (*bufff_cnt==12287)
	     {
	       *bufff_cnt=0;
	       for (int i=0; i<12288;i++)
	    	 {
	    	  // TODO
	    	 }
	     }
	   else
		 {
		   //*bufff_cnt++;
		 }

	  }

	   return 0;

}

static void exec_func(bool *func_start, u8 func_type, u8 *func_par)
{
   if (*func_start==true)
   {
	if (func_type==0)    ad9361_bist_loopback(ad9361_phy, 0x01&(*(func_par+0)));


	if (func_type==1)

	{
		if (*(uint8_t*)func_par == 1) 	ad9361_bist_tone(ad9361_phy, BIST_INJ_TX , 3, 0, 3);
		if (*(uint8_t*)func_par == 0) 	ad9361_bist_tone(ad9361_phy, BIST_DISABLE , 3, 0, 3);

	}

	/*{

		uint8_t spi_fuf[3] = {0x00,0x37,0x00};
		if ( spi_engine_write_and_read(spi_eng, &spi_fuf[0], 3) < 0)
			printf("spi_engine memory allocation failed\n\r", spi_fuf[2]);
		else if (spi_fuf[2]!=0x0a)
		    printf("spi_engine_rx_byte: 0x%02X\n\r", spi_fuf[2]);

	}*/

	if (func_type==2)	 ad9361_set_rx_fir_en_dis(ad9361_phy, 0x01&(*(func_par+0)));
	if (func_type==3)    ad9361_set_rx_rfdc_track_en_dis (ad9361_phy,*(uint8_t*)func_par);
	if (func_type==4)    ad9361_set_rx_bbdc_track_en_dis (ad9361_phy,*(uint8_t*)func_par);
	if (func_type==5)    ad9361_set_rx_quad_track_en_dis (ad9361_phy,*(uint8_t*)func_par);
	if (func_type==7)	 ad9361_set_tx_fir_en_dis(ad9361_phy, 0x01&(*(func_par+0)));
	if (func_type==6)    ad9361_set_tx_attenuation (ad9361_phy, 0, *(uint32_t*)func_par);
	if (func_type==8)    ad9361_set_tx_lo_freq (ad9361_phy, *(uint64_t*) func_par);
    if (func_type==9)    ad9361_set_rx_lo_freq (ad9361_phy, *(uint64_t*) func_par);
    if (func_type==10)   ad9361_set_rx_rf_gain (ad9361_phy, 0, *(uint32_t*)func_par);
    if (func_type==11)   ad9361_set_rx_gain_control_mode (ad9361_phy, 0, *(uint8_t*)func_par);
    if (func_type==12)   ad9361_tracking_control (ad9361_phy,*(uint8_t*) func_par,*(uint8_t*)(func_par+1),*(uint8_t*) (func_par+2) );
    if (func_type==13)   ad9361_set_tx_attenuation (ad9361_phy, 1, *(uint32_t*)func_par);

    if (func_type == 14) ad9361_fastlock_store(ad9361_phy, (bool)(*func_par >> 7), (uint8_t)*func_par & 0x7);
    if (func_type == 15) ad9361_fastlock_recall(ad9361_phy, (bool)(*func_par >> 7), (uint8_t)*func_par & 0x7);

    if (func_type == 16) print_dynamic_fdb(&bridge_netif);

    if (func_type == 17) spi_engine_npu_com_board_get_status(spi_eng_npu_com_board, &status_modem->status_npu);
    if (func_type == 18) spi_engine_npu_com_board_set_ena(spi_eng_npu_com_board,*(com_dev_type*) func_par, *(ena_type*)(func_par+1));
    //if (func_type == 19) spi_engine_npu_pow_board_get_status(spi_eng_npu_pow_board, &status_modem->status_npu);
    //if (func_type == 20) spi_engine_npu_pow_board_set_ena(spi_eng_npu_pow_board, *(pow_cntrl_type*) func_par, *(ena_type*)(func_par+1) );
    if (func_type == 19) spi_engine_npu_pow_board_set_f_diap(spi_eng_npu_com_board, spi_eng_npu_pow_board,  *(pow_cntrl_type*) func_par );

    //spi_engine_npu_pow_board_set_f_diap(spi_eng_npu_pow_board,  *(pow_cntrl_type*) func_par );

    if (func_type == 20)
    	{
    	   //printf("RRR");
    	   ppos_set_tangenta(*(ena_type*) func_par);
    	}


    //if (func_type == 20) spi_engine_npu_com_board_set_sd_on(spi_eng_npu_com_board);
    //if (func_type == 21) spi_engine_npu_com_board_set_sd_off(spi_eng_npu_com_board);
    //if (func_type == 22) spi_engine_npu_com_board_set_sd_status(spi_eng_npu_com_board);









    if (func_type==0x80)
    {
    	default_init_param.one_rx_one_tx_mode_use_rx_num = ((*(uint8_t*)func_par&0x02) == 0x02)?2:1;
   		default_init_param.one_rx_one_tx_mode_use_tx_num = ((*(uint8_t*)func_par&0x01) == 0x01)?2:1;
   		ad9361_init(&ad9361_phy, &default_init_param);
      }
	*func_start=false;
   }
}





void send_vid_packet_sized(uint8_t* vid, uint16_t size) {

	//xil_printf("svp: %d\n\r", size);

	udp_bind_netif (send_rx_data, &bridge_netif); //&udp_netif2);

	if (size == 0) {
		//xil_printf("send_vid_packet_sized: argument 'size' == 0 value\n\r");
		return;
	}


	if (size >= VID_PKT_MTU) {
		xil_printf("send_vid_packet_sized: argument 'size' >=VID_PKT_MTU -> %d \n\r", size);
		return;
	}


	if (vid[0] != 0x80) {
		return;
	}

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (!p) {



    	xil_printf("send_vid_packet_sized: argument 'size' ?????? -> %d \n\r", size);
        xil_printf("Failed to allocate pbuf for vid packet\n\r");
        return;
    }

    memcpy((uint8_t*) p->payload, vid, size);



    //udp_disconnect(send_rx_data);
    //err_t connect_err = udp_connect(send_rx_data, &RemoteAddr, RemotePortRx);
    //if (connect_err != ERR_OK) {
    //    xil_printf("Error connecting send_rx_data for reset: %d\n\r", connect_err);
    //    pbuf_free(p);
    //    return;
    //}

    udp_sendto(send_rx_data, p, &RemoteAddr, RemotePortRx);
    pbuf_free(p);

    //xil_printf("Reset packet sent\n\r");
}


/*

void send_debug_fifo_krt_gr_dat_empty() {

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 1, PBUF_RAM);
    if (!p) {
        xil_printf("Failed to allocate pbuf for krt_gr empty packet\n\r");
        return;
    }
    uint8_t tmp = 0x01;

    memcpy((uint8_t*) p->payload, &tmp, 1);

    udp_disconnect(debug_pcb);
    err_t connect_err = udp_connect(debug_pcb, &RemoteAddr, RemotePortTxDebug);
    if (connect_err != ERR_OK) {
        xil_printf("Error connecting send_rx_data for reset: %d\n\r", connect_err);
        pbuf_free(p);
        return;
    }

    udp_sendto(debug_pcb, p, &RemoteAddr, RemotePortTxDebug);
    pbuf_free(p);

    //xil_printf("Reset packet sent\n\r");
}
*/

/*

void send_debug_rx_check(uint8_t ch_tx, uint8_t ch_rx) {

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 2, PBUF_RAM);
    if (!p) {
        xil_printf("Failed to allocate pbuf for reset packet\n\r");
        return;
    }

    memcpy((uint8_t*) p->payload, &ch_tx, 1);
    memcpy((uint8_t*) p->payload+1, &ch_rx, 1);

    udp_disconnect(debug_pcb);
    err_t connect_err = udp_connect(debug_pcb, &RemoteAddr, RemotePortRxDebug);
    if (connect_err != ERR_OK) {
        xil_printf("Error connecting send_rx_data for reset: %d\n\r", connect_err);
        pbuf_free(p);
        return;
    }

    udp_sendto(debug_pcb, p, &RemoteAddr, RemotePortRxDebug);
    pbuf_free(p);
    //xil_printf("Reset packet sent\n\r");
}

*/





uint8_t IsRTTP(uint8_t* _array) {
    uint8_t result = 1;

    if (_array[0] != 0x80 && (_array[1] != 0x60 || _array[1] != 0xe0)) {
        result = 0;
    }

    return result;
}


void recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (!p) {
        //udp_disconnect(upcb);
        return;
    }




    deb_cnt_br++;








    //uint16_t dcnt = *((uint16_t*)p->payload);
    //uint8_t  cmd  = ((uint8_t *)p->payload)[2];
    u16_t    size = p->len;

    //xil_printf("CAL%d",size);



    //dcnt  !!!!!!!!!!!!!!!!!!!!!!!!!
    if (IsRTTP(p->payload)) {

    	//xil_printf("RTP%d\n\r",size);

    	//err_inc_recv_eth();
    	if (size<=1212) vid_push(&buf_vid, p->payload, size);
    	//XTime_GetTime(&last_response_time);
    }
	//free(data);
  	pbuf_free(p);
    return;
}









//Function to swap bytes for 32-bit integer
unsigned int swap32(unsigned int value) {

    return  ((value >> 24) & 0x000000FF) |
            ((value >> 8)  & 0x0000FF00) |
            ((value << 8)  & 0x00FF0000) |
            ((value << 24) & 0xFF000000);
}

unsigned short swap16(unsigned short value) {

    return  (((value >> 8)  & 0x00FF) |
             ((value << 8)  & 0xFF00));
}




int init_tx_rx_fifo() {

    XLlFifo_Config fifo_cfg = {FIFO_RX_DEV_BASE_ADDR};
    int status = XLlFifo_CfgInitialize(&fifo, &fifo_cfg, FIFO_RX_DEV_BASE_ADDR);
    if (status != XST_SUCCESS) {
        xil_printf("FIFO initialization failed.\n\r");
        return XST_FAILURE;
    }
    XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
    XLlFifo_Reset(&fifo);
    return XST_SUCCESS;
}

int init_deb_fifo() {
    XLlFifo_Config fifo_cfg = {FIFO_DEB_DEV_BASE_ADDR};
    int status = XLlFifo_CfgInitialize(&fifo_deb, &fifo_cfg, FIFO_DEB_DEV_BASE_ADDR);
    if (status != XST_SUCCESS) {
        xil_printf("FIFO initialization failed.\n\r");
        return XST_FAILURE;
    }
    XLlFifo_IntClear(&fifo_deb, 0xFFFFFFFF);
    XLlFifo_Reset(&fifo_deb);
    return XST_SUCCESS;
}




// Универсальная функция отправки байта
void send_response_byte(XUartPs *uart_ptr, u8 byte) {
    if (current_cmd_source == CMD_SRC_UART) {
        if (uart_ptr) {
            XUartPs_SendByte(uart_ptr->Config.BaseAddress, byte);
        }
    } else {
        if (global_reply_pcb) {
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 1, PBUF_RAM);
            if (p) {
                *((u8*)p->payload) = byte;
                udp_sendto(global_reply_pcb, p, &last_client_ip, last_client_port);
                pbuf_free(p);
            }
        }
    }
}

void process_cmd_3bytes(uint8_t *data, XUartPs *uart_ptr, struct no_os_spi_desc *spi_desc_ptr, bool *func_start, u8 *func_type, u8 *func_par)
{
    if((data[0] & 0x07) <= 0x04)
    {
         no_os_spi_write_and_read_11bit(spi_desc_ptr, &data[0], 0x1);

         if ((data[0] & 0x80) == 0) {
             send_response_byte(uart_ptr, data[2]);
         }


        // if ((data[0]&0x07)==0x04) fpga_reg[data[1]] = data[2];
    }

    if((data[0] & 0x07) > 0x04)
    {
          if ((data[0]==0x86) && (data[1]==0x01))
              *func_type = data[2];

          if ((data[0]==0x86) && (data[1]>=0x02) && (data[1]<0x12))
              func_par[data[1]-0x02] = data[2];

          if ((data[0]==0x86) && (data[1]==0x00))
              *func_start = ((data[2] & 0x01) == 0x01);
    }
}




void send_cmd()
{


  struct ppos_tx ppos_tx_par;
  ppos_tx_par.creg0.word = AXI_CNTRL_REG_READ(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0);


    if (ppos_tx_par.creg0.inside.run == 0)
   {

	   XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
	   XLlFifo_Reset(&fifo);
	   ppos_tx_par.creg0.inside.run = 1;
	   AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, ppos_tx_par.creg0.word);
       return;
   }




 if (ppos_tx_par.creg0.inside.run == 1) {


u32 size = 450;
u32 fifo_size = XLlFifo_iTxVacancy(&fifo);

if (fifo_size >= size   ) {

 struct  cmd_pkt* output;

 if (cmd_out->is_data) output = cmd_out->pkt;
 else return;

 uint16_t sw_cnt_s_pkt = swap16(cnt_s_pkt);

 memcpy((uint8_t*)output + 6, &sw_cnt_s_pkt , sizeof(uint16_t));
 cnt_s_pkt+=1;

u32 word;
u32 check = 0;


for (int i = 0; i < size; i++) {

			 memcpy(&word, (uint8_t*)output + (i * 4), sizeof(word));

			 if (i == size - 1)
			 {
				 XLlFifo_TxPutWord(&fifo, check);
			 }
			 else
			 {
				 word = swap32(word);
				 check+=word;
			     XLlFifo_TxPutWord(&fifo, word);
			 }
		 }

 XLlFifo_iTxSetLen(&fifo, (size)*4);
 cmd_out->is_data = false;

 }
	   }




 return;

}





