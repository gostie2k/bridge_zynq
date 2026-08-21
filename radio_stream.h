#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include "lwip/netif.h"
#include "lwip/pbuf.h"

// Init
void radio_stream_init(void);

// Main functions
err_t radio_linkoutput(struct netif *netif, struct pbuf *p);
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);
void radio_tx_periodic_flush(void);
static void tx_push_block(const uint8_t *src, uint32_t len);

void tx_process();

// Read all ready blocks from RX FIFO (call from main loop)
void drain_rx_fifo(void);

// Main poll: drain RX + push TX (call from main loop)
void radio_poll(void);

// Debug print
void radio_stream_print_stats(void);
void radio_stream_reset_stats(void);

#endif

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

// ---- Loopback (debug without radio channel) ----
// When on, TX blocks don't go to FPGA,
// they feed the RX parser directly.
void radio_stream_set_loopback(int enable);
int  radio_stream_get_loopback(void);
void radio_stream_reset_rx_ids(void);  /* reset pkt/seq gap counters before test */
