#pragma once

#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "xil_printf.h"
#include "axi_cntrl_reg/axi_cntrl_reg.h"

/* ===================================================================
 * Log debug level (runtime, можно менять на ходу через 0x90 0x46):
 *   0 = silent (no logs)
 *   1 = basic events (packet rx, errors)
 *   2 = verbose (+ hex-dump, register values)
 *   3 = hot-path trace (включает LOG_HOT в radio_stream/bridge_func и т.п.)
 *
 * Дефолтное значение задаётся LOG_DEBUG_DEFAULT (или 2 если не определено).
 * =================================================================== */
#ifndef LOG_DEBUG_DEFAULT
#define LOG_DEBUG_DEFAULT  2
#endif

extern volatile int g_log_level;

#define LOG1(fmt, ...)  do { if (g_log_level >= 1) xil_printf(fmt, ##__VA_ARGS__); } while (0)
#define LOG2(fmt, ...)  do { if (g_log_level >= 2) xil_printf(fmt, ##__VA_ARGS__); } while (0)
#define LOG_HOT(fmt, ...)  do { if (g_log_level >= 3) xil_printf(fmt, ##__VA_ARGS__); } while (0)

/* UDP command handler: dispatches 0xA0 (cregs), 0x90 (legacy proto),
 * and ucnt-prefixed legacy commands (firewall, IP). */
void cmd_udp_callback(void *arg, struct udp_pcb *upcb,
                      struct pbuf *p, const ip_addr_t *addr, u16_t port);
