#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* ===================================================================
 *  Hardware watchdog (XScuWdt) + software heartbeat supervision
 *  ------------------------------------------------------------------
 * Purpose: if any hot-path module hangs, PS gets a
 * hard reset via WDRESETREQ -> SLCR.
 *
 * Architecture:
 *   - wdt_init() configures XScuWdt with timeout WDT_TIMEOUT_MS.
 *   - wdt_start() starts the timer. After that you MUST
 *     call wdt_kick() periodically, more often than WDT_TIMEOUT_MS.
 *   - each hot-path module (main loop, lwIP RX, radio TX/RX)
 *     periodically calls its wdt_hb_*() - a software heartbeat.
 *   - wdt_kick() checks the age of all heartbeats. If any is
 *     stale (age > threshold) it does NOT reload the timer. After WDT_TIMEOUT_MS
 *     PS gets a soft reset.
 *
 * Under a debugger (JTAG) define WDT_DISABLED globally (e.g.
 * via -DWDT_DISABLED in the Vitis Debug config) - all APIs become no-ops.
 *
 * For hang-test diagnostics (when reset does not happen and you need
 * to find the cause) define WDT_DIAG_HANG - wdt_simulate_hang() will
 * print WDT/GIC/CPSR state every 200 ms.
 * =================================================================== */

#ifndef WDT_TIMEOUT_MS
#define WDT_TIMEOUT_MS  2000u    /* soft reset when no kick arrives */
#endif

/* Whether WDT is on by default at start. 1 = live mode (in the field catches
 * rare hangs and reboots), 0 = off (JTAG debug / diagnostics without reboots).
 * Runtime toggle - wdt_set_enabled() / UDP 0x90 0x4C. */
#ifndef WDT_ENABLED_DEFAULT
#define WDT_ENABLED_DEFAULT  1
#endif

/* Staleness thresholds for each heartbeat, in milliseconds.
 * If a module has not checked in for longer - it is considered hung.
 *
 * Note: these thresholds must be LARGER than the normal duration of
 * one main-loop iteration, otherwise false resets. SPI to AD9361
 * and heavy UDP commands can take hundreds of ms. */
#define WDT_HB_MAIN_MAX_MS  1000u   /* main loop - one pass is usually <100ms */
#define WDT_HB_LWIP_MAX_MS  1500u   /* xemacif_input may be silent when idle */
#define WDT_HB_TX_MAX_MS    5000u   /* radio TX may idle */
#define WDT_HB_RX_MAX_MS    5000u   /* radio RX may idle */

/* Throttle for wdt_progress_kick() - timer reload from hot-path on
 * real packet-processing progress. Must be noticeably smaller than
 * WDT_TIMEOUT_MS so a long xemacif_input pass cannot drain the timer. */
#define WDT_PROGRESS_KICK_MS  250u

/* Which heartbeats are CHECKED.
 * MAIN - mandatory. Enable the others when sure they are
 * generated in normal operation. */
#define WDT_CHECK_MAIN   1
#define WDT_CHECK_LWIP   0   /* off: no traffic when idle */
#define WDT_CHECK_TX     0   /* off: radio TX is optional */
#define WDT_CHECK_RX     0   /* off: radio RX is optional */

/* ===================================================================
 *  API
 * =================================================================== */

/* Forward declaration - avoid pulling all of xscugic.h into the header. */
struct XScuGic;

/* Init XScuWdt in TIMER mode + register ISR.
 * ISR on timeout pulses SLCR.PSS_RST_CTRL.SOFT_RST -> full PS reset
 * with FSBL restart (NOT just a CPU reset as in watchdog-mode).
 *
 * Requires an already-initialized XScuGic.
 * Returns 0 on success, !=0 on error. */
int  wdt_init(struct XScuGic *gic);

/* Start the timer. After this you must kick periodically. */
void wdt_start(void);

/* Stop the timer (for debugging). */
void wdt_stop(void);

/* Runtime enable/disable WDT (toggle via UDP 0x90 0x4C). enable!=0 -> start (if
 * initialized and not yet started), enable==0 -> stop (timer stopped,
 * no reboots). Safe to call repeatedly. */
void wdt_set_enabled(int enable);

/* 1 if WDT is currently on (timer ticking). */
int  wdt_is_enabled(void);

/* Check heartbeats and, if OK, reload the timer.
 * Call from the main loop at the end of each iteration. */
void wdt_kick(void);

/* Timer reload from hot-path on REAL processing progress.
 * Purpose: under overload xemacif_input may clear a large GEM-RX backlog
 * in one call and not return to the main loop for seconds - then the
 * hardware WDT (reloaded only in wdt_kick() from main loop) drains
 * to zero and gives a false reboot. Calling from the per-packet path (radio_linkoutput)
 * refreshes the MAIN heartbeat and reloads the timer, throttled to no more than
 * WDT_PROGRESS_KICK_MS. A real hang (no progress reaches here) is still
 * caught. Cheap: on throttle hit - one XTime read + compare. */
void wdt_progress_kick(void);

/* Diagnostics: how many times wdt_progress_kick was called (BEFORE throttle) - grows
 * if the bridge per-frame path really runs. Compare with kick_count (actual
 * timer reloads) in wdt_stats. */
uint32_t wdt_get_progress_kick_calls(void);

/* Heartbeats - call from the matching hot-path functions.
 * Cheap: one volatile store + XTime read. */
void wdt_hb_main(void);
void wdt_hb_lwip(void);
void wdt_hb_tx(void);
void wdt_hb_rx(void);

/* ===================================================================
 * Diagnostics
 * =================================================================== */

typedef struct {
    uint32_t reboot_status;     /* SLCR.REBOOT_STATUS read at startup */
    uint32_t wdt_reset_flag;    /* 1 if the previous reset was from WDT */
    uint32_t kick_count;        /* count of successful kicks */
    uint32_t starve_count;      /* count of "heartbeat went stale" cases */
    uint32_t last_starve_mask;  /* bit mask of heartbeats that went stale last time */
    uint32_t uptime_sec;        /* seconds since wdt_start */
    uint8_t  crash_section;     /* main_section at crash time */
    uint8_t  crash_oldest_hb;   /* which HB was oldest at reset (WDT_HB_BIT_*) */
    uint8_t  crash_starve_mask; /* bitmask of HBs that were stale at reset */
    uint8_t  crash_valid;       /* 1 if crash-info was read from SLCR scratch */
} wdt_stats_t;

/* Heartbeat bit mask (for last_starve_mask) */
#define WDT_HB_BIT_MAIN  (1u << 0)
#define WDT_HB_BIT_LWIP  (1u << 1)
#define WDT_HB_BIT_TX    (1u << 2)
#define WDT_HB_BIT_RX    (1u << 3)

/* ===================================================================
 * Section markers - where the main loop is now
 *  ------------------------------------------------------------------
 * Before each key operation the main loop calls wdt_section_mark().
 * On WDT-ISR we save the last section in SLCR scratch -
 * after reboot you can see what exactly stalled.
 * 4 bits -> up to 16 sections. Want more - extend to 5 bits and shift
 * the fields in SLCR scratch (see watchdog.c).
 * =================================================================== */
typedef enum {
    WDT_SEC_NONE = 0,
    WDT_SEC_LWIP_INPUT,    /* xemacif_input */
    WDT_SEC_LWIP_TMR,      /* sys_check_timeouts */
    WDT_SEC_FIFO_DEB,      /* fifo_deb_read + send_deb (IQ stream) */
    WDT_SEC_RADIO_POLL,    /* radio_poll, drain_rx_fifo, tx_process */
    WDT_SEC_STATUS_SEND,   /* send_modem_status (once per sec) */
    WDT_SEC_EXEC_FUNC,     /* exec_func - NPU/AD9361/SPI operations */
    WDT_SEC_AD9361_SPI,    /* ad9361_get_temperature via no-OS driver */
    WDT_SEC_VID_SEND,      /* send_vid_packet_* */
    WDT_SEC_OTHER,         /* everything not called out */
    WDT_SEC_AD9361_TEMP,   /* ad9361_get_temperature (no-OS API) */
    WDT_SEC_XADC,          /* XSysMon_GetAdcData (Zynq XADC) */
    WDT_SEC_SPI_RSSI,      /* no_os_spi_write_and_read via desc_tmp (rssi/gain) */
    WDT_SEC_RADIO_TX,      /* radio_linkoutput / tx_push_block (via bridge) */
    WDT_SEC_POST_LWIP,     /* code between xemacif_input and FIFO_DEB */
    WDT_SEC_MAX = 15
} wdt_section_t;

void wdt_section_mark(wdt_section_t s);

/* Current section (for save/restore around ISR/BSP hooks). */
uint8_t wdt_section_get(void);

/* DIAG (B, r8): repurpose dead section-IDs to localize a master freeze on the
 * normal GEM RX/TX path (BSP hooks - see docs/bsp_diag_patch.md).
 * Dead in the current build (SPI off): AD9361_SPI(7), AD9361_TEMP(10), SPI_RSSI(12).
 * XADC(11) do NOT touch - active. crash_section 0x40 shows them by ORIGINAL
 * name - remember the mapping: */
#define WDT_SEC_GEM_INPUT  WDT_SEC_AD9361_SPI    /* 7  -> xemacpsif_input before netif->input */
#define WDT_SEC_GEM_RXISR  WDT_SEC_VID_SEND      /* 8  → emacps_recv_handler (RX-ISR while(1)) */
#define WDT_SEC_GEM_TXOUT  WDT_SEC_AD9361_TEMP   /* 10 → low_level_output (GEM-TX) */
#define WDT_SEC_GEM_SENT   WDT_SEC_SPI_RSSI      /* 12 -> process_sent_bds (TX-done ISR) [r8: excluded] */

/* Section name for log/UI. */
const char *wdt_section_name(uint8_t s);

void wdt_get_stats(wdt_stats_t *out);

/* Save and clear SLCR.REBOOT_STATUS. Call ONCE as early as
 * early in main() before init - otherwise the next write to REBOOT_STATUS
 * may overwrite the cause. */
void wdt_capture_reboot_status(void);

/* Hang simulation - infinite loop with kick disabled.
 * Used for testing via UDP 0x90. Does not return. */
void wdt_simulate_hang(void) __attribute__((noreturn));

/* Direct PS soft reset via SLCR, bypassing WDT/ISR.
 * Used for diagnostics: if this call resets PS but
 * wdt_simulate_hang does not - the problem is in ISR/GIC.
 * Does not return. */
void wdt_force_reset_now(void) __attribute__((noreturn));

#endif /* WATCHDOG_H */
