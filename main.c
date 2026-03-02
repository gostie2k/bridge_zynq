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



#include "radio_stream.h"
#include "lwip/timeouts.h"
#include <stdio.h>
#include "platform.h"
#include "lwip/err.h"
#include "lwip/udp.h"
#include "netif/xadapter.h"
#include "xtime_l.h"
#include "xllfifo.h"
#include "xil_printf.h"

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
#include "netif/bridgeif.h"
#include "firewall.h"


//struct circular_buffer_a;
vid_buf*   buf_vid;
vid_out_buf* vid_out;
struct vid_pkt* zero_vid_pkt;

cmd_buf*   buf_cmd;
cmd_out_buf* cmd_out;
struct cmd_pkt* zero_cmd_pkt;

u32 temp_rx_words[47]; // Буфер для чтения 47 слов (188 байт) из FIFO
    uint8_t temp_rx_block[188]; // Буфер для байтов
    int rx_word_idx;

u16 deb_cnt_br=0;
u16 deb_cnt_rf=0;

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
extern struct netif bridge_netif;

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

u16_t RemotePort = 8;
u16_t RemotePortRx = 18;
u16_t RemotePortTxDebug = 28;
u16_t RemotePortRxDebug = 58;
u16_t RemotePortDeb = 78;

u16_t RemotePortStatus = 5007;


//------MERGE--------------
struct udp_pcb *cmd_udp_pcb;

u16_t CmdPort = 60012;  // порт для приема команд управления
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


XTime cur_time, mem_time ,last_response_time;

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
void send_modem_status(struct stat_modem* status);

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

















struct ad9361_rf_phy *ad9361_phy;
struct no_os_spi_desc desc_tmp;

//------------------------------MERGE-------------------------------------------------------------------

void cmd_udp_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{

	xil_printf("[CMD_UDP] pkt from %d.%d.%d.%d:%d len=%d data[0]=0x%02X\r\n",
	           ip4_addr1(addr), ip4_addr2(addr), ip4_addr3(addr), ip4_addr4(addr),
	           port, p->len, ((uint8_t*)p->payload)[0]);
	   extern struct ad9361_rf_phy *ad9361_phy;
	    extern struct no_os_spi_desc desc_tmp;

    if (p == NULL) {
        //xil_printf("[UDP_CMD] ERROR: Received NULL pbuf\n\r");
        return;
    }

    uint8_t *data = (uint8_t*)p->payload;
    uint16_t len = p->len;

    //xil_printf("\n\r========== UDP COMMAND RECEIVED ==========\n\r");
    //xil_printf("[UDP_CMD] Source IP: %d.%d.%d.%d:%d\n\r",  ip4_addr1(addr), ip4_addr2(addr),ip4_addr3(addr), ip4_addr4(addr), port);
    //xil_printf("[UDP_CMD] Packet length: %d bytes\n\r", len);

    // RAW HEX dump
    //xil_printf("[UDP_CMD] RAW data: ");
    //for (int i = 0; i < len && i < 32; i++) {
        //xil_printf("%02X ", data[i]);
       // if ((i + 1) % 16 == 0) //xil_printf("\n\r                    ");
    //}
    //if (len > 32) //xil_printf("... (truncated)");
    //xil_printf("\n\r");

    // ========== ПРОТОКОЛ 0x90 - НОВЫЕ КОМАНДЫ ==========
    if (len >= 2 && data[0] == 0x90) {
        uint8_t sub = data[1];
        //xil_printf("[UDP_CMD] Protocol 0x90, sub=0x%02X\n\r", sub);

        switch(sub) {
            // ==================== КОМАНДЫ ЗАПИСИ ====================

        case 0x02:  // Set TX frequency (WRITE)
            if (len == 10) {
                uint64_t freq = 0;

                for (int i = 0; i < 8; i++) {
                    freq |= ((uint64_t)data[2 + i]) << (i * 8);
                }

                //xil_printf("[UDP TX FREQ] Parsed: 0x%08lX%08lX\n\r", (uint32_t)(freq >> 32),(uint32_t)(freq & 0xFFFFFFFF));

                if (freq < 70000000ULL || freq > 6000000000ULL) {
                    //xil_printf("[UDP TX FREQ] ERROR: out of range (70MHz-6GHz)\n\r");
                    break;
                }

                int32_t result = ad9361_set_tx_lo_freq(ad9361_phy, freq);

                if (result == 0) {
                    uint64_t actual_freq = 0;
                    ad9361_get_tx_lo_freq(ad9361_phy, &actual_freq);
                    //xil_printf("[UDP TX FREQ] SUCCESS: actual=0x%08lX%08lX\n\r", (uint32_t)(actual_freq >> 32), (uint32_t)(actual_freq & 0xFFFFFFFF));
                } //else {
                    //xil_printf("[UDP TX FREQ] ERROR: ad9361_set_tx_lo_freq returned %ld\n\r", result);
                //}
            } //else {
                //xil_printf("[UDP TX FREQ] ERROR: needs 10 bytes, got %d\n\r", len);
            //}
            break;


        case 0x03:  // Set RX frequency
            if (len == 10) {
                //xil_printf("[UDP RX FREQ] RAW bytes from Qt: ");
                //for (int i = 0; i < 8; i++) {
                    //xil_printf("%02X ", data[2 + i]);
                //}
                //xil_printf("\n\r");

                uint64_t freq = 0;

                //  ДЕТАЛЬНАЯ СБОРКА С ОТЛАДКОЙ
                for (int i = 0; i < 8; i++) {
                    uint64_t byte_val = (uint64_t)data[2 + i];
                    uint64_t shifted = byte_val << (i * 8);
                    freq |= shifted;

                    //xil_printf("[UDP RX FREQ] i=%d: byte=0x%02X, shift=%d bits, shifted=0x%016llX, freq=0x%016llX (%llu)\n\r", i, data[2 + i], i * 8, shifted, freq, freq);
                }

                //xil_printf("[UDP RX FREQ] FINAL parsed freq: %llu Hz (0x%016llX)\n\r", freq, freq);

                if (freq < 70000000ULL || freq > 6000000000ULL) {
                    //xil_printf("[UDP RX FREQ] ERROR: freq %llu Hz out of range (70MHz-6GHz)\n\r", freq);
                    break;
                }

                int32_t result = ad9361_set_rx_lo_freq(ad9361_phy, freq);

                if (result == 0) {
                    uint64_t actual_freq = 0;
                    ad9361_get_rx_lo_freq(ad9361_phy, &actual_freq);
                    //xil_printf("[UDP RX FREQ] SUCCESS: set=%llu Hz, actual=%llu Hz\n\r", freq, actual_freq);
                }// else {
                    //xil_printf("[UDP RX FREQ] ERROR: ad9361_set_rx_lo_freq returned %d\n\r", result);
                //}
            } //else {
                //xil_printf("[UDP RX FREQ] ERROR: needs 10 bytes, got %d\n\r", len);
            //}
            break;


        case 0x04:  // Set TX attenuation (WRITE)
            if (len == 6) {
                uint32_t atten_db = 0;

                for (int i = 0; i < 4; i++) {
                    atten_db |= ((uint32_t)data[2 + i]) << (i * 8);
                }

                uint32_t atten_mdb = atten_db * 100;

                //xil_printf("[UDP TX ATT] Parsed: %lu dB (%lu mdB)\n\r", atten_db, atten_mdb);

                if (atten_db > 890) {
                    //xil_printf("[UDP TX ATT] ERROR: max 89 dB\n\r");
                    break;
                }

                int32_t result = ad9361_set_tx_attenuation(ad9361_phy, 1, atten_mdb);

                //if (result == 0) {
                    //xil_printf("[UDP TX ATT] SUCCESS: %lu dB\n\r", atten_db);
                //} else {
                    //xil_printf("[UDP TX ATT] ERROR: ad9361_set_tx_attenuation returned %ld\n\r", result);
                //}
            } //else {
                //xil_printf("[UDP TX ATT] ERROR: needs 6 bytes, got %d\n\r", len);
           // }
            break;

        case 0x05:  // Test generator (WRITE)
            if (len == 7) {
                uint32_t fcode = 0;
                for (int i = 0; i < 4; i++) {
                    fcode |= ((uint32_t)data[2 + i]) << (i * 8);
                }
                uint8_t mask = data[6];

                uint8_t d[4];
                d[0] = 0x84; d[1] = 0x18; d[2] = (fcode >> 24) & 0xFF;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                d[1] = 0x19; d[2] = (fcode >> 16) & 0xFF;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                d[1] = 0x1a; d[2] = (fcode >> 8) & 0xFF;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                d[1] = 0x1b; d[2] = fcode & 0xFF;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                d[1] = 0x00; d[2] = 0x08;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                d[1] = 0x01; d[2] = (mask)?0xC0:0x90;
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                //xil_printf("[UDP] Test gen: fcode=0x%08lX, mask=0x%02X\n\r", fcode, mask);
            } //else {
                //xil_printf("[UDP] ERROR: Test gen needs 7 bytes, got %d\n\r", len);
            //}
            break;

        case 0x06:  // RX modulation (WRITE)
            if (len == 3) {
                uint8_t mod_val = data[2];
                uint8_t d[4] = {0x84, 0x17, mod_val, 0};
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
                //xil_printf("[UDP] RX modulation par[23] = 0x%02X\n\r", mod_val);
            }// else {
                //xil_printf("[UDP] ERROR: RX mod needs 3 bytes, got %d\n\r", len);
            //}
            break;

        case 0x07:  // TX modulation (WRITE)
            if (len == 3) {
                uint8_t mod_val = data[2];
                uint8_t d[4] = {0x84, 0x16, mod_val, 0};
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
                //xil_printf("[UDP] TX modulation par[22] = 0x%02X\n\r", mod_val);
            } //else {
                //xil_printf("[UDP] ERROR: TX mod needs 3 bytes, got %d\n\r", len);
            //}
            break;

        // ==================== КОМАНДЫ ЧТЕНИЯ (READ) ====================

        case 0x10:  // Read TX frequency (READ)
            if (len == 2) {
                uint64_t tx_freq = 0;
                ad9361_get_tx_lo_freq(ad9361_phy, &tx_freq);

                //xil_printf("[UDP READ TX FREQ] Got 0x%08lX%08lX\n\r",(uint32_t)(tx_freq >> 32),(uint32_t)(tx_freq & 0xFFFFFFFF));

                struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 10, PBUF_RAM);
                if (resp) {
                    uint8_t *resp_data = (uint8_t*)resp->payload;
                    resp_data[0] = 0x90;
                    resp_data[1] = 0x10;

                    for (int i = 0; i < 8; i++) {
                        resp_data[2 + i] = (tx_freq >> (i * 8)) & 0xFF;
                    }

                    //xil_printf("[UDP READ TX FREQ] Sending: ");
                   // for (int i = 0; i < 10; i++) {
                        //xil_printf("%02X ", resp_data[i]);
                    //}
                    //xil_printf("\n\r");

                    udp_sendto(upcb, resp, addr, port);
                    pbuf_free(resp);
                }
            } //else {
                //xil_printf("[UDP READ TX FREQ] ERROR: needs 2 bytes, got %d\n\r", len);
           // }
            break;

//        case 0x08:  // Set GPIO/UF control (WRITE)
//            if (len == 3) {
//                uint8_t qt_mask = data[2];
//
//                //xil_printf("[UDP GPIO] Received Qt mask: 0x%02X\n\r", qt_mask);
//                //xil_printf("[UDP GPIO]   bit0 (Power):     %d\n\r", (qt_mask >> 0) & 1);
//                //xil_printf("[UDP GPIO]   bit1-2 (FreqSel): %d\n\r", (qt_mask >> 1) & 3);
//                //xil_printf("[UDP GPIO]   bit3 (Tangenta):  %d (%s)\n\r",
//                           (qt_mask >> 3) & 1,
//                           (qt_mask & 0x08) ? "PRD" : "PRM");
//
//                //  ФОРМИРУЕМ par[40] из маски Qt
//                uint8_t gpio_val = 0;
//
//                // 1. Питание УФ: Qt bit0 → GPIO bit1 (AB_GPIO5R, M14)
//                if (qt_mask & 0x01) {
//                    gpio_val |= (1 << 1);
//                }
//
//                // 2. Тангента: Qt bit3 → GPIO bit2 (ПРД) или bit4 (ПРМ)
//                bool is_prd = (qt_mask & 0x08) != 0;
//                if (is_prd) {
//                    gpio_val |= (1 << 2);  // AB_GPIO4R (L15) - ПРД
//                    gpio_val &= ~(1 << 4); // сбрасываем AB_GPIO2R (ПРМ)
//                } else {
//                    gpio_val |= (1 << 4);  // AB_GPIO2R (N15) - ПРМ
//                    gpio_val &= ~(1 << 2); // сбрасываем AB_GPIO4R (ПРД)
//                }
//
//                // 3. Частотный селектор: Qt bits[2:1] → GPIO bit0 или bit3
//                uint8_t freq_sel = (qt_mask >> 1) & 0x03;
//
//                if (freq_sel == 0) {  // 3400-3600 МГц
//                    gpio_val |= (1 << 0);  // AB_GPIO6R (M15)
//                    gpio_val &= ~(1 << 3); // сбрасываем AB_GPIO3R
//                    //xil_printf("[UDP GPIO] Freq: 3400-3600 MHz\n\r");
//                } else if (freq_sel == 1) {  // 3700-3900 МГц
//                    gpio_val |= (1 << 3);  // AB_GPIO3R (N16)
//                    gpio_val &= ~(1 << 0); // сбрасываем AB_GPIO6R
//                    //xil_printf("[UDP GPIO] Freq: 3700-3900 MHz\n\r");
//                } else {
//                    // Другие индексы - оба в 0
//                    gpio_val &= ~((1 << 0) | (1 << 3));
//                    //xil_printf("[UDP GPIO] Freq: NONE\n\r");
//                }
//
//                //xil_printf("[UDP GPIO] Resulting par[40]:\n\r");
//                //xil_printf("  bit4 (AB_GPIO2R/ПРМ):   %d\n\r", (gpio_val >> 4) & 1);
//                //xil_printf("  bit3 (AB_GPIO3R/3.7G):  %d\n\r", (gpio_val >> 3) & 1);
//                //xil_printf("  bit2 (AB_GPIO4R/ПРД):   %d\n\r", (gpio_val >> 2) & 1);
//                //xil_printf("  bit1 (AB_GPIO5R/POWER): %d\n\r", (gpio_val >> 1) & 1);
//                //xil_printf("  bit0 (AB_GPIO6R/3.4G):  %d\n\r", (gpio_val >> 0) & 1);
//
//                //  ЗАПИСЫВАЕМ в FPGA par[40]
//                uint8_t d[4] = {0x84, 0x28, gpio_val, 0};  // 0x28 = 40 decimal
//                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
//
//                // Проверка readback
//                usleep(10);
//                uint8_t r[4] = {0x85, 0x28, 0x00, 0x00};
//                no_os_spi_write_and_read_11bit(&desc_tmp, r, 1);
//
//                if (r[2] == gpio_val) {
//                    //xil_printf("[UDP GPIO]  par[40] = 0x%02X written successfully\n\r", gpio_val);
//                } else {
//                    //xil_printf("[UDP GPIO]  MISMATCH: wrote 0x%02X, read 0x%02X\n\r",
//                               gpio_val, r[2]);
//                }
//            } else {
//                //xil_printf("[UDP GPIO] ERROR: needs 3 bytes, got %d\n\r", len);
//            }
//            break;

        case 0x08:  // Set GPIO/UF control (WRITE)
            if (len == 3) {
                uint8_t qt_mask = data[2];

                //xil_printf("[UDP GPIO] Received Qt mask: 0x%02X\n\r", qt_mask);
                //xil_printf("[UDP GPIO]   bit0 (Power):        %d\n\r", (qt_mask >> 0) & 1);
                //xil_printf("[UDP GPIO]   bit1-2 (FreqSel):    %d\n\r", (qt_mask >> 1) & 3);
                //xil_printf("[UDP GPIO]   bit3 (Tangenta):     %d (%s)\n\r",   (qt_mask >> 3) & 1,  (qt_mask & 0x08) ? "PRD" : "PRM");


                uint8_t gpio_val = 0;

                // bit 0: uf_pit (питание УФ)
                if (qt_mask & 0x01) {
                    gpio_val |= (1 << 0);
                }

                // bit 1: uf_tangenta (тангента: 0=ПРМ, 1=ПРД)
                if (qt_mask & 0x08) {  // Qt bit3
                    gpio_val |= (1 << 1);
                }

                // bit 2: uf_sel (частотный селектор: 0=3.4-3.6 ГГц, 1=3.7-3.9 ГГц)
                uint8_t freq_sel = (qt_mask >> 1) & 0x03;
                if (freq_sel == 1) {  // 3.7-3.9 ГГц
                    gpio_val |= (1 << 2);
                }
                // freq_sel == 0 → bit2 = 0 (3.4-3.6 ГГц)

                // bit 3: uf_tangenta_ena_avt
                // gpio_val |= (0 << 3);

                //xil_printf("[UDP GPIO] Resulting par[40]:\n\r");
                //xil_printf("  bit0 (uf_pit):              %d\n\r", (gpio_val >> 0) & 1);
                //xil_printf("  bit1 (uf_tangenta):         %d (%s)\n\r",    (gpio_val >> 1) & 1,      (gpio_val & 0x02) ? "PRD" : "PRM");
                //xil_printf("  bit2 (uf_sel):              %d (%s)\n\r",   (gpio_val >> 2) & 1,      (gpio_val & 0x04) ? "3.7-3.9 GHz" : "3.4-3.6 GHz");
                //xil_printf("  bit3 (uf_tangenta_ena_avt): %d\n\r", (gpio_val >> 3) & 1);

                uint8_t d[4] = {0x84, 0x28, gpio_val, 0};
                no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                usleep(10);
                uint8_t r[4] = {0x85, 0x28, 0x00, 0x00};
                no_os_spi_write_and_read_11bit(&desc_tmp, r, 1);

                //if (r[2] == gpio_val) {
                    //xil_printf("[UDP GPIO]  par[40] = 0x%02X written successfully\n\r", gpio_val);
                //} else {
                    //xil_printf("[UDP GPIO]   par[40]: wrote 0x%02X, read 0x%02X\n\r",  gpio_val, r[2]);
                    //xil_printf("[UDP GPIO] Note: par[40] might be write-only or partially implemented\n\r");
                //}
            } //else {
                //xil_printf("[UDP GPIO] ERROR: needs 3 bytes, got %d\n\r", len);
            //}
            break;



        case 0x11:  // Read RX frequency
            if (len == 2) {
                uint64_t rx_freq = 0;
                ad9361_get_rx_lo_freq(ad9361_phy, &rx_freq);

                //xil_printf("[UDP READ RX] ad9361_get_rx_lo_freq returned 0x%08lX%08lX\n\r",  (uint32_t)(rx_freq >> 32), (uint32_t)(rx_freq & 0xFFFFFFFF));

                struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 10, PBUF_RAM);
                if (resp) {
                    uint8_t *resp_data = (uint8_t*)resp->payload;
                    resp_data[0] = 0x90;
                    resp_data[1] = 0x11;

                    for (int i = 0; i < 8; i++) {
                        resp_data[2 + i] = (rx_freq >> (i * 8)) & 0xFF;
                    }

                    //xil_printf("[UDP READ RX] Sending packet: ");
                   // for (int i = 0; i < 10; i++) {
                        //xil_printf("%02X ", resp_data[i]);
                    //}
                    //xil_printf("\n\r");

                    udp_sendto(upcb, resp, addr, port);
                    pbuf_free(resp);

                    //xil_printf("[UDP READ RX] Packet sent\n\r");
                }
            } //else {
                //xil_printf("[UDP] ERROR: Read RX freq needs 2 bytes, got %d\n\r", len);
            //}
            break;


        case 0x12:  // Read TX attenuation (READ)
            if (len == 2) {
                uint32_t atten_mdb = 0;
                ad9361_get_tx_attenuation(ad9361_phy, 1, &atten_mdb);
                uint32_t atten_db = atten_mdb / 100;

                //xil_printf("[UDP READ TX ATT] Got %lu dB (%lu mdB)\n\r", atten_db, atten_mdb);

                struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 6, PBUF_RAM);
                if (resp) {
                    uint8_t *resp_data = (uint8_t*)resp->payload;
                    resp_data[0] = 0x90;
                    resp_data[1] = 0x12;

                    for (int i = 0; i < 4; i++) {
                        resp_data[2 + i] = (atten_db >> (i * 8)) & 0xFF;
                    }

                    //xil_printf("[UDP READ TX ATT] Sending: ");
                    //for (int i = 0; i < 6; i++) {
                        //xil_printf("%02X ", resp_data[i]);
                    //}
                    //xil_printf("\n\r");

                    udp_sendto(upcb, resp, addr, port);
                    pbuf_free(resp);
                }
            } //else {
                //xil_printf("[UDP READ TX ATT] ERROR: needs 2 bytes, got %d\n\r", len);
            //}
            break;

            case 0x13:  // Read FPGA register (par[N])
                if (len == 3) {
                    uint8_t par_addr = data[2];

                    if (par_addr < 42) {  // par[0..41]
                        uint8_t d[4] = {0x85, par_addr, 0x00, 0x00};  // 0x85 = READ
                        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                        struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);
                        if (resp) {
                            uint8_t *resp_data = (uint8_t*)resp->payload;
                            resp_data[0] = 0x90;
                            resp_data[1] = 0x13;
                            resp_data[2] = par_addr;
                            resp_data[3] = d[2];  // прочитанное значение

                            udp_sendto(upcb, resp, addr, port);
                            pbuf_free(resp);

                            //xil_printf("[UDP] Read FPGA par[%d] = 0x%02X (sent back)\n\r",   par_addr, d[2]);
                        }
                    } //else {
                        //xil_printf("[UDP] ERROR: Invalid par_addr %d (max 41)\n\r", par_addr);
                   // }
                } //else {
                    //xil_printf("[UDP] ERROR: Read FPGA par needs 3 bytes, got %d\n\r", len);
               // }
                break;

            default:
                //xil_printf("[UDP] ERROR: Unknown sub=0x%02X\n\r", sub);
                break;

            case 0x14:  // Set AD9361 Profile & FHSS mode (WRITE)
                if (len == 3) {
                    uint8_t profile_val = data[2];





                    // Распаковка битовых полей для отладки
                    uint8_t profile = profile_val & 0x07;        // биты [2:0]
                    uint8_t txnrx = (profile_val >> 6) & 0x01;      // биты [7:6]


                    ad9361_fastlock_store(ad9361_phy, (bool) txnrx ,(uint32_t) profile );



                    ////xil_printf("[UDP] AD Profile: par[42] = 0x%02X\n\r", profile_val);
                    ////xil_printf("  AD9361 profile [2:0] = %d\n\r", ad9361_profile);
                    ////xil_printf("  FHSS mode [7:6] = %d\n\r", fhss_mode);

                    // Запись в FPGA par[42]
                    //uint8_t d[4] = {0x84, 42, profile_val, 0};  // 0x84 = WRITE, 42 = адрес регистра
                    //no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                    ////xil_printf("[UDP] Profile written successfully\n\r");
                } //else {
                    //xil_printf("[UDP] ERROR: AD Profile needs 3 bytes, got %d\n\r", len);
              // }
                break;

            case 0x15:
            	if (len == 3){
                    uint8_t fhss = data[2];
                    uint8_t fhss_toggle = 0;



                    if (fhss == 0x01) {
                    	fhss_toggle = 1;
                    }else
                    {
                    	fhss_toggle = 0;

                                  }


                    if(fhss_toggle)
                    {


                        uint8_t d[4] = {0x82, 0x5a, 0x03, 0};
                        no_os_spi_write_and_read(&desc_tmp, d, 1);
                        d[1] = 0x9a;
                        no_os_spi_write_and_read(&desc_tmp, d, 1);

                        d[0] =0x84; d[1] = 0x2a; d[2] = 0x40;
                        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);


                    }
                    else
                    {

                    	 uint8_t d[4] = {0x82, 0x5a, 0x00, 0};

                         no_os_spi_write_and_read(&desc_tmp, d, 1);
                         d[1] = 0x9a;
                         no_os_spi_write_and_read(&desc_tmp, d, 1);

                         d[0] =0x84; d[1] = 0x2a; d[2] = 0x00;
                         no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

                    }

            	}

            	break;



            	/*

            case 0x16:  // Set RX2 GAIN (WRITE)
                     if (len == 10) {
                         uint64_t freq = 0;

                         for (int i = 0; i < 8; i++) {
                             freq |= ((uint64_t)data[2 + i]) << (i * 8);
                         }

                         //xil_printf("[UDP TX FREQ] Parsed: 0x%08lX%08lX\n\r", (uint32_t)(freq >> 32),(uint32_t)(freq & 0xFFFFFFFF));

                         if (freq < 70000000ULL || freq > 6000000000ULL) {
                             //xil_printf("[UDP TX FREQ] ERROR: out of range (70MHz-6GHz)\n\r");
                             break;
                         }

                         int32_t result = ad9361_set_tx_lo_freq(ad9361_phy, freq);

                         if (result == 0) {
                             uint64_t actual_freq = 0;
                             ad9361_get_tx_lo_freq(ad9361_phy, &actual_freq);
                             //xil_printf("[UDP TX FREQ] SUCCESS: actual=0x%08lX%08lX\n\r", (uint32_t)(actual_freq >> 32), (uint32_t)(actual_freq & 0xFFFFFFFF));
                         } //else {
                             //xil_printf("[UDP TX FREQ] ERROR: ad9361_set_tx_lo_freq returned %ld\n\r", result);
                         //}
                     } //else {
                         //xil_printf("[UDP TX FREQ] ERROR: needs 10 bytes, got %d\n\r", len);
                     //}
                     break;


            	*/








        }






        //xil_printf("==========================================\n\r\n\r");
        pbuf_free(p);
        return;
    }


    else if (len >= 3)
       {
           uint16_t ucnt_rx = ((uint16_t)data[0] << 8) | data[1];
           uint8_t  cmd     = data[2];

           switch (cmd) {

           /* --- 0x05: Установка параметров сетевого фильтра (11 байт) --- */
           case 0x05:
               if (len >= 11) {
                   uint32_t ip_mask = ((uint32_t)data[3] << 24) |
                                      ((uint32_t)data[4] << 16) |
                                      ((uint32_t)data[5] << 8)  |
                                      ((uint32_t)data[6]);
                   uint8_t arp_en  = data[7];
                   uint8_t icmp_en = data[8];
                   uint8_t tcp_en  = data[9];
                   uint8_t udp_en  = data[10];

                   firewall_set_config(ip_mask, arp_en, icmp_en, tcp_en, udp_en);

                   xil_printf("[CMD 0x05] ucnt=%d mask=%d.%d.%d.%d ARP=%d ICMP=%d TCP=%d UDP=%d\r\n",
                              ucnt_rx,
                              (ip_mask >> 24) & 0xFF, (ip_mask >> 16) & 0xFF,
                              (ip_mask >> 8) & 0xFF,  ip_mask & 0xFF,
                              arp_en, icmp_en, tcp_en, udp_en);
               }
               break;

           /* --- 0x06: Запрос параметров сетевого фильтра (запрос 3 байт, ответ 11 байт) --- */
           case 0x06:
           {
               firewall_config_t cfg = firewall_get_config();

               struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 11, PBUF_RAM);
               if (resp) {
                   uint8_t *r = (uint8_t *)resp->payload;
                   /* ucnt — возвращаем тот же счётчик что пришёл */
                   r[0] = data[0];
                   r[1] = data[1];
                   r[2] = 0x06;
                   /* ip_mask big-endian */
                   r[3] = (cfg.ip_mask >> 24) & 0xFF;
                   r[4] = (cfg.ip_mask >> 16) & 0xFF;
                   r[5] = (cfg.ip_mask >> 8)  & 0xFF;
                   r[6] =  cfg.ip_mask        & 0xFF;
                   r[7]  = cfg.arp_en;
                   r[8]  = cfg.icmp_en;
                   r[9]  = cfg.tcp_en;
                   r[10] = cfg.udp_en;

                   udp_sendto(upcb, resp, addr, port);
                   pbuf_free(resp);

                   xil_printf("[CMD 0x06] Query from ucnt=%d -> responded\r\n", ucnt_rx);
               }
               break;
           }

           /* --- 0x07: Установка IP через broadcast (15 байт) --- */
           case 0x07:
               if (len >= 15) {
                   /* IP адреса приходят в network byte order (big-endian) */
                   ip4_addr_t new_ip, new_mask, new_gw;

                   /* Собираем из big-endian в host order, затем используем IP4_ADDR_SET */
                   uint32_t ip_val = ((uint32_t)data[3]  << 24) | ((uint32_t)data[4]  << 16) |
                                     ((uint32_t)data[5]  << 8)  | ((uint32_t)data[6]);
                   uint32_t nm_val = ((uint32_t)data[7]  << 24) | ((uint32_t)data[8]  << 16) |
                                     ((uint32_t)data[9]  << 8)  | ((uint32_t)data[10]);
                   uint32_t gw_val = ((uint32_t)data[11] << 24) | ((uint32_t)data[12] << 16) |
                                     ((uint32_t)data[13] << 8)  | ((uint32_t)data[14]);

                   IP4_ADDR(&new_ip,   (ip_val >> 24) & 0xFF, (ip_val >> 16) & 0xFF,
                                       (ip_val >> 8)  & 0xFF,  ip_val & 0xFF);
                   IP4_ADDR(&new_mask, (nm_val >> 24) & 0xFF, (nm_val >> 16) & 0xFF,
                                       (nm_val >> 8)  & 0xFF,  nm_val & 0xFF);
                   IP4_ADDR(&new_gw,   (gw_val >> 24) & 0xFF, (gw_val >> 16) & 0xFF,
                                       (gw_val >> 8)  & 0xFF,  gw_val & 0xFF);

                   netif_set_addr(&bridge_netif, &new_ip, &new_mask, &new_gw);

                   xil_printf("[CMD 0x07] IP changed to %d.%d.%d.%d  mask %d.%d.%d.%d  gw %d.%d.%d.%d\r\n",
                              (ip_val >> 24) & 0xFF, (ip_val >> 16) & 0xFF,
                              (ip_val >> 8)  & 0xFF,  ip_val & 0xFF,
                              (nm_val >> 24) & 0xFF, (nm_val >> 16) & 0xFF,
                              (nm_val >> 8)  & 0xFF,  nm_val & 0xFF,
                              (gw_val >> 24) & 0xFF, (gw_val >> 16) & 0xFF,
                              (gw_val >> 8)  & 0xFF,  gw_val & 0xFF);
               }
               break;

           default:
               break;
           }

           pbuf_free(p);
           return;
       }

       else
       {
           pbuf_free(p);
           return;
       }



    //if (len >= 1) {
    //    func_type = data[0];
        //xil_printf("[UDP_CMD] func_type = 0x%02X (%d)\n\r", func_type, func_type);

    //    uint16_t par_len = (len - 1) > 16 ? 16 : (len - 1);
    //    if (par_len > 0) {
            //xil_printf("[UDP_CMD] func_par[%d]: ", par_len);
    //        for (int i = 0; i < par_len; i++) {
    //            func_par[i] = data[i + 1];
                //xil_printf("%02X ", func_par[i]);
    //        }
            //xil_printf("\n\r");

            //xil_printf("[UDP_CMD] func_par (decimal): ");
           // for (int i = 0; i < par_len; i++) {
                //xil_printf("%d ", func_par[i]);
          //  }
            //xil_printf("\n\r");
    //    }// else {
            //xil_printf("[UDP_CMD] No parameters (func_par empty)\n\r");
       // }

   //     for (int i = par_len; i < 16; i++) {
   //         func_par[i] = 0;
   //     }

  //      func_start = true;
        //xil_printf("[UDP_CMD] Command activated! func_start = TRUE\n\r");
  //  } //else {
        //xil_printf("[UDP_CMD] ERROR: Packet too short (len < 1)\n\r");
  //  }

    //xil_printf("==========================================\n\r\n\r");
 //   pbuf_free(p);




}
//------------------------------MERGE-------------------------------------------------------------------



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
    #else

  #ifdef IS_MAKET_PKRV
	/* RF Port Control */
	0,		//rx_rf_port_input_select *** adi,rx-rf-port-input-select
	0,		//tx_rf_port_input_select *** adi,tx-rf-port-input-select
  #endif
#endif


	/* TX Attenuation Control */
	50000,	//tx_attenuation_mdB *** adi,tx-attenuation-mdB
	0,		//update_tx_gain_in_alert_enable *** adi,update-tx-gain-in-alert-enable


//#ifdef IS_ANTSDR
	/* Reference Clock Control */
	1,		//xo_disable_use_ext_refclk_enable *** adi,xo-disable-use-ext-refclk-enable
//#else

//#ifdef IS_MAKET_PKRV
//	/* Reference Clock Control */
//	0,		//xo_disable_use_ext_refclk_enable *** adi,xo-disable-use-ext-refclk-enable
//#endif
//#endif

	//{8, 2500},	//dcxo_coarse_and_fine_tune[2] *** adi,dcxo-coarse-and-fine-tune
	{8, 5920},	//dcxo_coarse_and_fine_tune[2] *** adi,dcxo-coarse-and-fine-tune
	CLKOUT_DISABLE,	//clk_output_mode_select *** adi,clk-output-mode-select
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
bool is_pow_rx = false;
bool is_pow_cntrl_tx = false;




static void IntrHandler(void *CallBackRef, u32
Bank, u32 Status)
{
 u32 size;
 XGpioPs *Gpioint = (XGpioPs *) CallBackRef;




if (  ((Status&0x20000000) == 0x20000000)&&(Bank == 3))
{
  XGpioPs_IntrClearPin(Gpioint, 54+61);
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

  pow_rx = rssi;
  is_pow_rx = true; */

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


if (  ((Status&0x10000000) == 0x10000000)&&(Bank == 3))
{
  XGpioPs_IntrClearPin(Gpioint, 54+60);
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
  is_pow_cntrl_tx = true; */

 // printf("#2 %.2f dB \n\r", rssi);

  return;
}



if (  ((Status&0x08000000) == 0x08000000)&&(Bank == 3))
{
 XGpioPs_IntrClearPin(Gpioint, 54+59);
 /*
 is_cntrl_pkt_end = true;*/
 return;
}







if (((Status&0x80000000) == 0x80000000)&&(Bank == 3))
{

 XGpioPs_IntrClearPin(Gpioint, 54+63);

if (!is_run) return;

//u16 tx_len;
//tx_len=fpga_regs[12];
//tx_len<<=8;
//tx_len+=fpga_regs[13];



#ifdef IS_SLAVE
//if (tx_len == 240000) //SLAVE
//if ((fpga_regs[8]&0x80) == 0x00) //SLAVE
//{
 struct  vid_pkt* voutput;
 if (vid_out->is_data) voutput = vid_out->pkt;
 else voutput = zero_vid_pkt;

 // add cmd pkt
 struct  cmd_pkt* coutput;
 if (cmd_out->is_data) coutput = cmd_out->pkt;
 else coutput = zero_cmd_pkt;

 memcpy((uint8_t*)voutput + 6, &cnt_s_pkt ,sizeof(uint16_t));

 cnt_s_pkt+=1;

 memcpy(voutput->head + 100, coutput->head+20  ,sizeof(uint16_t) );
 memcpy(voutput->head + 102, coutput->head+22  ,sizeof(uint8_t) );
 memcpy(voutput->tel, coutput->data  , 1518 );

 size = 2250;
 u32 fifo_size = XLlFifo_iTxVacancy(&fifo);
 //if (fifo_size >= 6+size   ) ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 if (fifo_size >= size   )
 {

	 u32 word;
	 u32 check = 0;

//	 for (int i = 0; i < 6; ++i) ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//	 {
//		memcpy(&word, (uint8_t*) &pream_new[0]+i*4,sizeof(word));
//		word = swap32(word);
//		XLlFifo_TxPutWord(&fifo, word);
//	 }



 for (int i = 0; i < size; ++i) {



	  memcpy(&word, (uint8_t*)voutput + (i * 4), sizeof(word));

	 if (i == size - 1)
				 {
		             word = swap32(check);
					 XLlFifo_TxPutWord(&fifo, word);
				 }
	 else
	 {
			 check+=word;
			 word = swap32(word);
			 XLlFifo_TxPutWord(&fifo, word);
	 }

	 }
 //XLlFifo_iTxSetLen(&fifo, (6+size)*4); ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 XLlFifo_iTxSetLen(&fifo, (size)*4); ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// }
 if (vid_out->is_data) vid_out->is_data = false;
 if (cmd_out->is_data) cmd_out->is_data = false;
}
 return;
//}

#endif




#ifdef IS_MASTER
//if ((fpga_regs[8]&0x80) == 0x80) //MASTER
//if (tx_len == 4800) // MASTER
//{

 size = 450;

u32 fifo_size = XLlFifo_iTxVacancy(&fifo);
//if (fifo_size >= 6+size   ) { ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
if (fifo_size >= size   ) {
 struct  cmd_pkt* output;

 if (cmd_out->is_data) output = cmd_out->pkt;
 else output = zero_cmd_pkt;

 uint16_t sw_cnt_s_pkt = swap16(cnt_s_pkt);

 memcpy((uint8_t*)output + 6, &sw_cnt_s_pkt , sizeof(uint16_t));
 cnt_s_pkt+=1;

u32 word;
u32 check = 0;


//for (int i = 0; i < 6; i++) ///////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//{
//	memcpy(&word,(uint8_t*) &pream_new[0]+i*4, sizeof(word));
//	word = swap32(word);
//	XLlFifo_TxPutWord(&fifo, word);
//}



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

 if (cmd_out->is_data) cmd_out->is_data = false;


 }


 return;
//}
#endif



}

}








		 // TX ONLY DEBUG
		 /*
		 {
			 data_vid* output_restored = malloc(sizeof(data_vid));
			 memset(output_restored, 0x00, sizeof(data_vid));

			 for (int i=0; i < size; ++i) {
				 uint32_t RxWord;
				 memcpy(&RxWord, start + i * 4, sizeof(RxWord));
				 memcpy((uint8_t*)(output_restored) + (i * 4), &RxWord, sizeof(RxWord));
			 }

			  uint16_t sized[5] = {1269, 1269, 1269, 1269, 1269};
			  memcpy((uint8_t*)sized + 0, (uint8_t*)(output_restored->head) + 80, sizeof(uint16_t));
			  memcpy((uint8_t*)sized + 2, (uint8_t*)(output_restored->head) + 82, sizeof(uint16_t));
			  memcpy((uint8_t*)sized + 4, (uint8_t*)(output_restored->head) + 84, sizeof(uint16_t));
			  memcpy((uint8_t*)sized + 6, (uint8_t*)(output_restored->head) + 86, sizeof(uint16_t));
			  memcpy((uint8_t*)sized + 8, (uint8_t*)(output_restored->head) + 88, sizeof(uint16_t));

			  // OK
//			  send_vid_packet_sized_deb(output_restored->vid_0, sized[0]);
//			  send_vid_packet_sized_deb(output_restored->vid_1, sized[1]);
//			  send_vid_packet_sized_deb(output_restored->vid_2, sized[2]);
//			  send_vid_packet_sized_deb(output_restored->vid_3, sized[3]);
//			  send_vid_packet_sized_deb(output_restored->vid_4, sized[4]);

			  free(start);
			  free(output_restored);
		 }*/

		 //err_inc_send_fifo();
	//	 XLlFifo_iTxSetLen(&fifo, size*4);
	 //}
 //}

 //free(output);
 //cnt_gr++;

 //if (vid_out->is_data) vid_out->is_data = false;


 //return; // next code is prev ver.



// if (fifo_krt_gr_dat[rad].gr_cnt == (uint16_t) (cnt_gr- cnt_gr_start_sent))
// {

//	 if (is_val_buf(&fifo_krt_gr_dat[0] , rad))
//	 {
//
//	 size=(uint8_t*)&fifo_krt_gr_dat[rad].gr_size-(uint8_t*)&fifo_krt_gr_dat[rad];
//     size/=4;


//     uint8_t check=0;
//     for (int i=0;i<7353; i++) check+=*((uint8_t*)&fifo_krt_gr_dat[rad].tel+i);
//     fifo_krt_gr_dat[rad].head[16]=check;


//     if (XLlFifo_iTxVacancy(&fifo)) {
//  	   for(int i=0;i<size;i++)//2304
//  	   {
//           u32 word;
//           memcpy(&word, ((uint32_t*)&fifo_krt_gr_dat[rad]) +i, 4);

//           word=swap32(word);

          // word = i;


//  		   XLlFifo_TxPutWord(&fifo, word);
//  	   }
//           XLlFifo_iTxSetLen(&fifo, size*4);//9216
//  }
// }

//    clear_val_buf(&fifo_krt_gr_dat[0] , rad);
//    rad=(rad == FIFO_TX_DEPTH-1)?0:rad+1;

//    send_debug_fifo_krt_gr_dat();
// }
// else
// {
//	send_debug_fifo_krt_gr_dat_empty();
// }

// send_request();
// cnt_gr++;

//}















static XSysMon XADCMonInst;



int counter_err = 0;



struct stat_modem* status_modem;

bool led_tog =true;


int main()
{

	status_modem = 	stat_modem_init();


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





     init_platform();

     knk_init_platform(&Uart1_Ps, &Gpio_Ps, &ScuGic, IntrHandler);









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



    //uint8_t d[] = {0x80,0x00,0x81,0x00};
    //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

    //usleep(50);

    //d[0]=0x80;d[1]=0x00;d[2]=0x00;
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




  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004418, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004458, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x00004498, 0x00000002);
  axi_adc_write((struct axi_adc*) ad9361_phy->rx_adc, 0x000044d8, 0x00000002);


//ad9361
 uint8_t d[] = {0x80,0xfb,0x48,0x00};
 //d[0]=0x80;d[1]=0xfb;d[2]=0x48;
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
#ifdef IS_MASTER
//par[8],par[9],par[10],par[11] = 0x80,0x124f80
d[0]=0x84;d[1]=0x08;d[2]=0xc0;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x09;d[2]=0x12;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0a;d[2]=0x4f;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0b;d[2]=0x80;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
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
#endif


#ifdef IS_SLAVE
//par[8],par[9],par[10],par[11] = 0x80,0x124f00
d[0]=0x84;d[1]=0x08;d[2]=0x40;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x09;d[2]=0x12;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0a;d[2]=0x4f;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x0b;d[2]=0x80;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
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
#endif



//par[5],par[6],par[20],par[21] = 0x1583d702
d[0]=0x84;d[1]=0x06;d[2]=0x15;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x05;d[2]=0x83;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x15;d[2]=0xd7;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);
d[0]=0x84;d[1]=0x14;d[2]=0x02;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[22] = 0x0a
d[0]=0x84;d[1]=0x16;d[2]=0x0a;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[23] = 0x0a
d[0]=0x84;d[1]=0x17;d[2]=0x0a;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);


//pax[2] = 0x99
d[0]=0x84;d[1]=0x02;d[2]=0xd9;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

//par[16],par[17],par[18],par[19] = 0x07d01194 ( txnrx_setu = 2000, txxnrx_hold = 4500 )

d[0]=0x84;d[1]=0x10;d[2]=0x07;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

d[0]=0x84;d[1]=0x11;d[2]=0xd0;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

d[0]=0x84;d[1]=0x12;d[2]=0x11;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

d[0]=0x84;d[1]=0x13;d[2]=0x94;
no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);







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





//XGpioPs_WritePin(&Gpio_Ps, 46, 1);

//platform_enable_interrupts();


#ifdef IS_OLD_PAR
#ifdef IS_MASTER
    IP4_ADDR(&RemoteAddr, 169, 254, 206, 124);
#endif
#ifdef IS_SLAVE
    IP4_ADDR(&RemoteAddr, 169, 254, 206, 224);
#endif
#endif


#ifndef IS_OLD_PAR
#ifdef IS_MASTER
    IP4_ADDR(&RemoteAddr, 192, 168, 250, 10);
#endif
#ifdef IS_SLAVE
    IP4_ADDR(&RemoteAddr, 192, 168, 250, 22);
#endif
#endif

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



    //send_pcb = udp_new();
    recv_pcb = udp_new();
    udp_bind_netif (recv_pcb, &bridge_netif); //&udp_netif2);
    status = udp_bind(recv_pcb, IP_ADDR_ANY, 5007);

    //status = udp_bind(recv_pcb, &RemoteAddr, 5007);

    udp_recv(recv_pcb, recv_callback, NULL);


    send_rx_data = udp_new(); // temp send video pkts
    udp_bind_netif (send_rx_data, &bridge_netif); //&udp_netif2);


    debug_pcb = udp_new(); // send status & debug pkts
    udp_bind_netif (debug_pcb, &bridge_netif); //&udp_netif2);

    cmd_udp_pcb = udp_new();
    ip_set_option(cmd_udp_pcb, SOF_BROADCAST);

       //udp_bind_netif(cmd_udp_pcb, &bridge_netif);
       udp_bind(cmd_udp_pcb, IP_ADDR_ANY, CmdPort);
       udp_recv(cmd_udp_pcb, cmd_udp_callback, NULL);
       xil_printf("[INIT] cmd_udp_pcb bound to port %d on bridge_netif\r\n", CmdPort);

    //status = udp_bind(debug_pcb, IP_ADDR_ANY, CmdPort);
    //udp_recv(debug_pcb, cmd_udp_callback, NULL);


    for (int i=0;i<FIFO_RX_DEPTH;i++) fifo_rx_buf[i].rx_is_val=0;


    //stt = true;

//    ad9361_bist_loopback(ad9361_phy, 1);
//    xil_printf("!!! WARNING: AD9361 DIGITAL LOOPBACK ENABLED !!!\n\r");

    platform_enable_interrupts();
    is_run = true;

    while(1)
        {
            //sys_check_timeouts();
    	//xemacif_input(&port1_bridge);

            xemacif_input(echo_netif);

        	vid_buf_exch(buf_vid, vid_out);
        	cmd_buf_exch(buf_cmd, cmd_out);

            // ====================================================================
            // НОВАЯ ЛОГИКА ПРИЕМА ИЗ РАДИО (FIFO -> Bridge)
            // ====================================================================
            if(XLlFifo_iRxOccupancy(&fifo) >= 47)
            {
                for(rx_word_idx = 0; rx_word_idx < 47; rx_word_idx++) {
                    temp_rx_words[rx_word_idx] = XLlFifo_RxGetWord(&fifo);
                }

                for(rx_word_idx = 0; rx_word_idx < 47; rx_word_idx++) {
                    u32 w = temp_rx_words[rx_word_idx];
                    temp_rx_block[rx_word_idx*4 + 0] = (uint8_t)(w & 0xFF);
                    temp_rx_block[rx_word_idx*4 + 1] = (uint8_t)((w >> 8) & 0xFF);
                    temp_rx_block[rx_word_idx*4 + 2] = (uint8_t)((w >> 16) & 0xFF);
                    temp_rx_block[rx_word_idx*4 + 3] = (uint8_t)((w >> 24) & 0xFF);
                }

                radio_input_block(temp_rx_block, &port3_bridge);

                deb_cnt_rf++;
            }

            // ====================================================================
            // СТАРАЯ ЛОГИКА (ОТКЛЮЧЕНА)
            // ====================================================================

            // fifo_rx_buf_write(&fifo_rx_buf[0],&fifo_rx_wad, &cmd_rx_buf[0]);
            // #ifdef IS_MASTER
            //    fifo_rx_buf_check(&fifo_rx_buf[0],&fifo_rx_wad);
            // #endif


            radio_tx_periodic_flush();

            //fifo_debug_uart_test(&Uart1_Ps);

        	cnt_uart_rx_buf = uart_cmd_chan(cnt_uart_rx_buf, &buf, &Uart1_Ps, &desc_tmp, &func_start, &func_type, &func_par[0], &fpga_regs[0]);
        	debug_ram_load(&bufff_cnt, &bufff[0], &Gpio_Ps, &Uart1_Ps);
        	exec_func(&func_start, func_type, &func_par[0]);


        	fifo_deb_read();


        	 XTime_GetTime(&cur_time);

        	 if (cur_time - mem_time >= XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ )
        	 {
                 sys_seconds++;
                 if (sys_seconds >= 60) {
                     sys_seconds = 0;
                     sys_minutes++;
                     if (sys_minutes >= 60) {
                         sys_minutes = 0;
                         sys_hours++;
                     }
                 }

                 if (bridge_netif.state != NULL) {
                      void *fdb_ptr = bridgeif_get_dynamic_fdb(&bridge_netif);
                      if (fdb_ptr != NULL) {
                          bridgeif_fdb_age(fdb_ptr);
                      }
                  }

                 xil_printf("[%02d:%02d:%02d] ETH: %d  RF: %d\r\n",
                            sys_hours, sys_minutes, sys_seconds,
                            deb_cnt_br, deb_cnt_rf);

                 radio_stream_print_stats();

        		 mem_time=cur_time;
        	 }

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

if (is_val) send_deb();


}


}




void send_deb() {


	udp_bind_netif (debug_pcb, &bridge_netif); //&udp_netif2);

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

    err_t out = udp_sendto(debug_pcb, p, &RemoteAddr, RemotePortDeb);

    if (out != ERR_OK) {
        xil_printf("Error udp_sendto debug_pcb: %d\n\r", out);
    }


    pbuf_free(p);
    //xil_printf("Reset packet sent\n\r");
}


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
						xil_printf("!is_val \n\r");
			            return;
			}



			 uint8_t check = 0;
			 uint16_t size;
			 memcpy(&size,  &cmd_rx_buf[0]+20  ,sizeof(uint16_t) );
			 memcpy(&check, &cmd_rx_buf[0]+22  ,sizeof(uint8_t) );

			 if ((size != 0)&&(size<=1518))
				 {
				 	   uint8_t check_solve = 0;
			           for (int k=0; k<size; k++) check_solve+= cmd_rx_buf[29+k];
				       //xil_printf("CMD_REC -> len = %d, check = %d check_solve = %d\n\r", size, check, check_solve);


				       if (check == check_solve)
				       {
				        struct pbuf *p;
				        p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
				        memcpy((uint8_t*) p->payload, &cmd_rx_buf[29], size);

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

		else




		{
			//err_inc_recv_fifo_wrong();


			xil_printf("! data wrong: ReceiveLength =  %08x\n\r", ReceiveLength);

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


    if (is_run == false) {
            pbuf_free(p);
            return;
        }

    deb_cnt_br++;

    radio_linkoutput(&port3_bridge, p);






    //uint16_t dcnt = *((uint16_t*)p->payload);
    //uint8_t  cmd  = ((uint8_t *)p->payload)[2];
   // u16_t    size = p->len;

    //xil_printf("CAL%d",size);



    //dcnt  !!!!!!!!!!!!!!!!!!!!!!!!!
   // if (IsRTTP(p->payload)) {

    	//xil_printf("RTP%d\n\r",size);

    	//err_inc_recv_eth();
    	//if (size<=1212) vid_push(&buf_vid, p->payload, size);
    	//XTime_GetTime(&last_response_time);
   // }
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




