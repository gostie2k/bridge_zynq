#pragma once

#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "xil_printf.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"
#include "modem.h"

 /* 0 = silent, 1 = events, 2 = verbose */

#ifndef LOG_DEBUG
#define LOG_DEBUG  2
#endif

#if LOG_DEBUG >= 1
  #define LOG1(fmt, ...)  xil_printf(fmt, ##__VA_ARGS__)
#else
  #define LOG1(fmt, ...)  ((void)0)
#endif

#if LOG_DEBUG >= 2
  #define LOG2(fmt, ...)  xil_printf(fmt, ##__VA_ARGS__)
#else
  #define LOG2(fmt, ...)  ((void)0)
#endif

/* protocol 0x90 + legacy ucnt */
void cmd_udp_callback(void *arg, struct udp_pcb *upcb,
                      struct pbuf *p, const ip_addr_t *addr, u16_t port);

static void handle_creg_exch(struct udp_pcb *upcb, uint8_t *data,
                          uint16_t len, const ip_addr_t *addr, u16_t port);

void send_modem_status(struct udp_pcb *upcb, struct stat_modem* status);
