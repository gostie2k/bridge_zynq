#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include "lwip/netif.h"
#include "lwip/pbuf.h"

#include "xllfifo.h"
#include "xllfifo_hw.h"

// Р�РЅРёС†РёР°Р»РёР·Р°С†ия
void radio_stream_init(void);

// РћСЃРЅРѕРІРЅС‹Рµ С„СѓРЅРєС†ии
err_t radio_linkoutput(struct netif *netif, struct pbuf *p);
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);
void radio_tx_periodic_flush(void);

void tx_process();

/* Parse what came from the FIFO. Two RX channels, so the FIFO is a parameter:
 * drain_rx_fifo       - data channel: diag + segment to the bridge;
 * drain_cntrl_tx_fifo - control channel: diag only. Call from the main loop. */
void drain_rx_fifo(XLlFifo* fifo_data);
void drain_cntrl_tx_fifo(XLlFifo* fifo_data);

// Р“Р»Р°РІРЅР°СЏ С„СѓРЅРєС†ия РѕРїСЂРѕСЃР°: drain RX + РґРѕСЃС‹Р»РєР° TX (РІС‹Р·С‹РІР°С‚СЊ РІ main loop)
void radio_poll(void);

// НОВАЯ Р¤РЈРќРљР¦Р�РЇ: РџРµС‡Р°С‚СЊ РѕС‚Р»Р°дки
void radio_stream_print_stats(void);
void radio_stream_reset_stats(void);

// Struct to pass counters out (main.c -> UDP reply)
typedef struct {
    uint32_t tx_pkts;
    uint32_t tx_blocks;
    uint32_t err_fifo_full;
    uint32_t rx_blocks;
    uint32_t err_seq_gap;
    uint32_t err_rx_overflow;
    uint32_t rx_pkts;
    uint32_t err_pkt_gap;
    uint32_t err_crc;
} radio_stats_t;

void radio_stream_get_stats(radio_stats_t *out);

/* ---- Loopback (отладка без радиоканала) ---- */
// РљРѕРіРґР° РІРєР»СЋС‡С‘РЅ вЂ” TX-Р±Р»оки РЅРµ СѓС…РѕРґСЏС‚ РІ FPGA,
// Р° СЃСЂР°Р·Сѓ РїРѕРґР°СЋС‚ся РЅР° РІС…од RX-РїР°СЂСЃРµСЂР°.
void radio_stream_set_loopback(int enable);
int  radio_stream_get_loopback(void);
void radio_stream_reset_rx_ids(void);  /* before a test run */

/* Queue / FIFO telemetry: counters and snapshots only, no behaviour change. */
typedef struct {
    uint32_t t_ms;            /* ms since boot, modem clock */
    uint32_t ring_size;       /* identifies the build */
    uint32_t tx_count;
    uint32_t tx_count_max;    /* high-water since last reset */
    uint32_t tx_blocks;       /* d(blocks)/dt = air rate */
    uint32_t tx_pkts;
    uint32_t err_tx_reject;   /* ring was full */
    uint32_t err_tx_stall;    /* TX FIFO full */
    uint32_t err_fifo_full;   /* dropped in send_block_to_pl */
    uint32_t fifo_tx_vacancy;
    uint32_t fifo_tx_depth;   /* words, read after reset */
    uint32_t rx_count;
    /* Telemetry appended, so the fields above keep their meaning */
    uint32_t tlm_ring_size;
    uint32_t tlm_count;
    uint32_t tlm_count_max;
    uint32_t tlm_pkts;
    uint32_t tlm_reject;
    /* Telemetry HW queue (MM_S_0): its depth is the telemetry latency floor */
    uint32_t tlm_fifo_vacancy;
    uint32_t tlm_fifo_depth;
} radio_queue_stats_t;

void radio_stream_get_queue_stats(radio_queue_stats_t *out);
void radio_stream_reset_queue_stats(void);
/* Called once from init_tx_rx_fifo: right after XLlFifo_Reset the TX vacancy
 * equals the full depth, which is the only way to learn it from software. */
void radio_stream_set_fifo_depth(uint32_t words);       /* telemetry FIFO */
void radio_stream_set_fifo_depth_data(uint32_t words);  /* data FIFO */

/* RX diag: [0]=current format frames, [1]=old, [2]=other length,
 * [3]=length of the last odd frame, [4]=FA CE marks found.
 * first16 = first 16 bytes of the last block. */
void radio_stream_get_rx_diag(uint32_t *out5, uint8_t *first16);

#endif /* RADIO_STREAM_H */
