#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include "lwip/netif.h"
#include "lwip/pbuf.h"

// Р�РЅРёС†РёР°Р»РёР·Р°С†РёСЏ
void radio_stream_init(void);

// РћСЃРЅРѕРІРЅС‹Рµ С„СѓРЅРєС†РёРё
err_t radio_linkoutput(struct netif *netif, struct pbuf *p);
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);
void radio_tx_periodic_flush(void);

void tx_process();

// Р§С‚РµРЅРёРµ РІСЃРµС… РіРѕС‚РѕРІС‹С… Р±Р»РѕРєРѕРІ РёР· RX FIFO (РІС‹Р·С‹РІР°С‚СЊ РёР· main loop)
void drain_rx_fifo(void);

// Р“Р»Р°РІРЅР°СЏ С„СѓРЅРєС†РёСЏ РѕРїСЂРѕСЃР°: drain RX + РґРѕСЃС‹Р»РєР° TX (РІС‹Р·С‹РІР°С‚СЊ РІ main loop)
void radio_poll(void);

// РќРћР’РђРЇ Р¤РЈРќРљР¦Р�РЇ: РџРµС‡Р°С‚СЊ РѕС‚Р»Р°РґРєРё
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

// ---- Loopback (РѕС‚Р»Р°РґРєР° Р±РµР· СЂР°РґРёРѕРєР°РЅР°Р»Р°) ----
// РљРѕРіРґР° РІРєР»СЋС‡С‘РЅ вЂ” TX-Р±Р»РѕРєРё РЅРµ СѓС…РѕРґСЏС‚ РІ FPGA,
// Р° СЃСЂР°Р·Сѓ РїРѕРґР°СЋС‚СЃСЏ РЅР° РІС…РѕРґ RX-РїР°СЂСЃРµСЂР°.
void radio_stream_set_loopback(int enable);
int  radio_stream_get_loopback(void);
void radio_stream_reset_rx_ids(void);  /* reset pkt/seq gap counters before test */

/* ---- Queue / FIFO telemetry (read-only, for the buffering measurements) ----
 * Everything here is a counter or a snapshot: enabling it does not change
 * behaviour, so a build carrying only this is a fair baseline. */
typedef struct {
    uint32_t t_ms;            /* ms since boot (modem clock, for exact dt)     */
    uint32_t ring_size;       /* RING_SIZE of THIS build - identifies M0 vs M1 */
    uint32_t tx_count;        /* bytes queued in tx_ring right now             */
    uint32_t tx_count_max;    /* high-water mark since last reset              */
    uint32_t tx_blocks;       /* blocks handed to PL: d(blocks)/dt = air rate  */
    uint32_t tx_pkts;         /* packets accepted into the ring                */
    uint32_t err_tx_reject;   /* packets dropped: ring was full                */
    uint32_t err_tx_stall;    /* tx_process aborted: TX FIFO full              */
    uint32_t err_fifo_full;   /* block dropped inside send_block_to_pl         */
    uint32_t fifo_tx_vacancy; /* free words in TX FIFO now                     */
    uint32_t fifo_tx_depth;   /* TX FIFO depth in words (captured after reset) */
    uint32_t rx_count;        /* bytes queued in rx_ring right now             */
    /* Telemetry stream, appended so the fields above keep their meaning and
     * baselines taken before prioritisation stay comparable. */
    uint32_t tlm_ring_size;
    uint32_t tlm_count;
    uint32_t tlm_count_max;
    uint32_t tlm_pkts;
    uint32_t tlm_reject;
} radio_queue_stats_t;

void radio_stream_get_queue_stats(radio_queue_stats_t *out);
void radio_stream_reset_queue_stats(void);
/* Called once from init_tx_rx_fifo(): right after XLlFifo_Reset() the TX
 * vacancy equals the full depth - the only way to learn it from software. */
void radio_stream_set_fifo_depth(uint32_t words);

#endif /* RADIO_STREAM_H */
