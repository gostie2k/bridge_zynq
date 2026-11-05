#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* HW watchdog (XScuWdt) + software heartbeats (HB).
 * -DWDT_DISABLED: calls do nothing. -DWDT_DIAG_HANG: print WDT/GIC regs. */

#ifndef WDT_TIMEOUT_MS
#define WDT_TIMEOUT_MS  2000u
#endif

/* runtime switch: 0x90 0x4C */
#ifndef WDT_ENABLED_DEFAULT
#define WDT_ENABLED_DEFAULT  1
#endif

/* max HB age, ms; must exceed one main loop pass */
#define WDT_HB_MAIN_MAX_MS  1000u
#define WDT_HB_LWIP_MAX_MS  1500u
#define WDT_HB_TX_MAX_MS    5000u
#define WDT_HB_RX_MAX_MS    5000u

#define WDT_PROGRESS_KICK_MS  250u

#define WDT_CHECK_MAIN   1
#define WDT_CHECK_LWIP   0
#define WDT_CHECK_TX     0
#define WDT_CHECK_RX     0

struct XScuGic;

int  wdt_init(struct XScuGic *gic);   /* 0 = ok */
void wdt_start(void);
void wdt_stop(void);
void wdt_set_enabled(int enable);
int  wdt_is_enabled(void);
void wdt_kick(void);                  /* end of main loop */
void wdt_progress_kick(void);         /* from packet path */
uint32_t wdt_get_progress_kick_calls(void);

void wdt_hb_main(void);
void wdt_hb_lwip(void);
void wdt_hb_tx(void);
void wdt_hb_rx(void);

typedef struct {
    uint32_t reboot_status;
    uint32_t wdt_reset_flag;
    uint32_t kick_count;
    uint32_t starve_count;
    uint32_t last_starve_mask;
    uint32_t uptime_sec;
    uint8_t  crash_section;
    uint8_t  crash_oldest_hb;   /* index 0..3, 0xF = none */
    uint8_t  crash_starve_mask;
    uint8_t  crash_valid;
} wdt_stats_t;

#define WDT_HB_BIT_MAIN  (1u << 0)
#define WDT_HB_BIT_LWIP  (1u << 1)
#define WDT_HB_BIT_TX    (1u << 2)
#define WDT_HB_BIT_RX    (1u << 3)

/* Main loop section, saved to SLCR scratch on reset. 4 bits, 16 max. */
typedef enum {
    WDT_SEC_NONE = 0,
    WDT_SEC_LWIP_INPUT,
    WDT_SEC_LWIP_TMR,
    WDT_SEC_FIFO_DEB,      /* fifo_deb_read + send_deb */
    WDT_SEC_RADIO_POLL,    /* radio_poll, drain_rx_fifo, tx_process */
    WDT_SEC_STATUS_SEND,
    WDT_SEC_EXEC_FUNC,
    WDT_SEC_AD9361_SPI,
    WDT_SEC_VID_SEND,
    WDT_SEC_OTHER,
    WDT_SEC_AD9361_TEMP,
    WDT_SEC_XADC,
    WDT_SEC_SPI_RSSI,      /* rssi/gain over desc_tmp */
    WDT_SEC_RADIO_TX,
    WDT_SEC_POST_LWIP,     /* between xemacif_input and FIFO_DEB */
    WDT_SEC_MAX = 15
} wdt_section_t;

void wdt_section_mark(wdt_section_t s);
uint8_t wdt_section_get(void);

/* Old names for BSP patches, unused */
#define WDT_SEC_GEM_INPUT  WDT_SEC_AD9361_SPI
#define WDT_SEC_GEM_RXISR  WDT_SEC_VID_SEND
#define WDT_SEC_GEM_TXOUT  WDT_SEC_AD9361_TEMP
#define WDT_SEC_GEM_SENT   WDT_SEC_SPI_RSSI

const char *wdt_section_name(uint8_t s);

void wdt_get_stats(wdt_stats_t *out);

/* Call once, early in main(), before other init */
void wdt_capture_reboot_status(void);

void wdt_simulate_hang(void) __attribute__((noreturn));
void wdt_force_reset_now(void) __attribute__((noreturn));

#endif /* WATCHDOG_H */
