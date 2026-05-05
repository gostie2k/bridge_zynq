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
static void tx_push_block(const uint8_t *src, uint32_t len);

void tx_process();

// Р§С‚РµРЅРёРµ РІСЃРµС… РіРѕС‚РѕРІС‹С… Р±Р»РѕРєРѕРІ РёР· RX FIFO (РІС‹Р·С‹РІР°С‚СЊ РёР· main loop)
void drain_rx_fifo(void);

// Р“Р»Р°РІРЅР°СЏ С„СѓРЅРєС†РёСЏ РѕРїСЂРѕСЃР°: drain RX + РґРѕСЃС‹Р»РєР° TX (РІС‹Р·С‹РІР°С‚СЊ РІ main loop)
void radio_poll(void);

// РќРћР’РђРЇ Р¤РЈРќРљР¦Р�РЇ: РџРµС‡Р°С‚СЊ РѕС‚Р»Р°РґРєРё
void radio_stream_print_stats(void);
void radio_stream_reset_stats(void);

#endif

// РЎС‚СЂСѓРєС‚СѓСЂР° РґР»СЏ РїРµСЂРµРґР°С‡Рё СЃС‡С‘С‚С‡РёРєРѕРІ РЅР°СЂСѓР¶Сѓ (РІ main.c в†’ UDP-РѕС‚РІРµС‚)
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
void radio_stream_reset_rx_ids(void);  /* СЃР±СЂРѕСЃ pkt/seq gap СЃС‡С‘С‚С‡РёРєРѕРІ РїРµСЂРµРґ С‚РµСЃС‚РѕРј */
