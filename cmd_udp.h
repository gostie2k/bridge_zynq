#pragma once

#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "xil_printf.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"

/* ===================================================================
 * Log debug level:
 *   0 = silent (no logs)
 *   1 = basic events (packet rx, errors)
 *   2 = verbose (+ hex-dump, register values)
 * =================================================================== */
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

/* UDP command handler: dispatches 0xA0 (cregs), 0x90 (legacy proto),
 * and ucnt-prefixed legacy commands (firewall, IP). */
void cmd_udp_callback(void *arg, struct udp_pcb *upcb,
                      struct pbuf *p, const ip_addr_t *addr, u16_t port);
