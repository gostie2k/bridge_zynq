#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include "lwip/netif.h"
#include "lwip/pbuf.h"

// Инициализация
void radio_stream_init(void);

// Основные функции
err_t radio_linkoutput(struct netif *netif, struct pbuf *p);
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);
void radio_tx_periodic_flush(void);

// НОВАЯ ФУНКЦИЯ: Печать отладки
void radio_stream_print_stats(void);

#endif
