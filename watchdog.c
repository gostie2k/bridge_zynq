#include "watchdog.h"

#include "xscuwdt.h"
#include "xscuwdt_hw.h"
#include "xscugic.h"
#include "xparameters.h"
#include "xtime_l.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xil_exception.h"

#ifdef WDT_DISABLED
void wdt_capture_reboot_status(void) {}
int  wdt_init(struct XScuGic *gic) { (void)gic; xil_printf("[WDT] disabled (debug build)\r\n"); return 0; }
void wdt_start(void) {}
void wdt_stop(void) {}
void wdt_set_enabled(int enable) { (void)enable; }
int  wdt_is_enabled(void) { return 0; }
void wdt_kick(void) {}
void wdt_progress_kick(void) {}
uint32_t wdt_get_progress_kick_calls(void) { return 0; }
void wdt_hb_main(void) {}
void wdt_hb_lwip(void) {}
void wdt_hb_tx(void) {}
void wdt_hb_rx(void) {}
void wdt_get_stats(wdt_stats_t *out) { if (out) { __builtin_memset(out, 0, sizeof(*out)); } }
void wdt_simulate_hang(void) { for(;;) __asm__ volatile("nop"); }
void wdt_force_reset_now(void) { for(;;) __asm__ volatile("nop"); }
void wdt_section_mark(wdt_section_t s) { (void)s; }
uint8_t wdt_section_get(void) { return 0; }
const char *wdt_section_name(uint8_t s) { (void)s; return "?"; }
#else

/* SLCR REBOOT_STATUS: cause in [22:16], scratch [15:0] survives the reset */
#define SLCR_BASE              0xF8000000u
#define SLCR_UNLOCK_OFFSET     0x00000008u
#define SLCR_LOCK_OFFSET       0x00000004u
#define SLCR_UNLOCK_KEY        0x0000DF0Du
#define SLCR_LOCK_KEY          0x0000767Bu
#define SLCR_REBOOT_STATUS_OFF 0x00000258u

#define SLCR_BIT_SLC_RST       (1u << 16)
#define SLCR_BIT_DBG_RST       (1u << 17)
#define SLCR_BIT_SRST_B        (1u << 18)
#define SLCR_BIT_AWDT1_RST     (1u << 19)
#define SLCR_BIT_AWDT0_RST     (1u << 20)
#define SLCR_BIT_SWDT_RST      (1u << 21)
#define SLCR_BIT_POR           (1u << 22)
#define SLCR_REASON_MASK       (0x7Fu << 16)   /* [22:16] */

#define SLCR_PSS_RST_CTRL_OFF  0x00000200u
#define SLCR_SOFT_RST_BIT      0x00000001u

/* Scratch [15:0]: bit 4 our flag, [3:0] starve mask, [11:8] section,
 * [15:12] oldest HB. FSBL keeps [15:0], it uses only [23:16]. */
#define SLCR_WDT_MAGIC_BIT     (1u << 4)
#define SLCR_CRASH_STARVE_MASK 0x0000000Fu  /* bits 3..0 */
#define SLCR_CRASH_SECTION_SHIFT  8u
#define SLCR_CRASH_SECTION_MASK   0x00000F00u
#define SLCR_CRASH_OLDEST_SHIFT   12u
#define SLCR_CRASH_OLDEST_MASK    0x0000F000u
#define SLCR_CRASH_INFO_BITS      (SLCR_CRASH_STARVE_MASK | SLCR_CRASH_SECTION_MASK | SLCR_CRASH_OLDEST_MASK | SLCR_WDT_MAGIC_BIT)

/* WDT clock = PERIPHCLK/(prescaler+1); 255 -> ~1302 ticks/ms at 333 MHz */
#define WDT_PRESCALER          255u

static XScuWdt g_wdt;
static int     g_wdt_inited  = 0;
static int     g_wdt_started = 0;
static uint32_t g_wdt_load_value = 0;

#ifndef STDOUT_BASEADDRESS
#define STDOUT_BASEADDRESS 0xE0001000u   /* UART1 */
#endif
#define XUARTPS_SR_OFFSET   0x2Cu
#define XUARTPS_SR_TXEMPTY  (1u << 3)
#define XUARTPS_SR_TACTIVE  (1u << 11)

static void wdt_uart_flush(void)
{
    /* ~10 ms at 666 MHz */
    volatile uint32_t spin = 6600000u;
    while (spin--) {
        uint32_t sr = Xil_In32(STDOUT_BASEADDRESS + XUARTPS_SR_OFFSET);
        if ((sr & XUARTPS_SR_TXEMPTY) && !(sr & XUARTPS_SR_TACTIVE)) {
            return;
        }
    }
}

static void wdt_write_crash_to_slcr(uint8_t section, uint8_t oldest_hb, uint8_t starve_mask)
{
    uint32_t crash_bits = SLCR_WDT_MAGIC_BIT
                        | ((uint32_t)(starve_mask & 0xFu))
                        | (((uint32_t)section & 0xFu)   << SLCR_CRASH_SECTION_SHIFT)
                        | (((uint32_t)oldest_hb & 0xFu) << SLCR_CRASH_OLDEST_SHIFT);

    Xil_Out32(SLCR_BASE + SLCR_UNLOCK_OFFSET, SLCR_UNLOCK_KEY);
    uint32_t rs = Xil_In32(SLCR_BASE + SLCR_REBOOT_STATUS_OFF);
    rs = (rs & ~SLCR_CRASH_INFO_BITS) | crash_bits;
    Xil_Out32(SLCR_BASE + SLCR_REBOOT_STATUS_OFF, rs);
}

/* Crash info must be written first */
static void wdt_trigger_soft_reset(void)  __attribute__((noreturn));
static void wdt_trigger_soft_reset(void)
{
    Xil_ExceptionDisable();

    wdt_uart_flush();

    Xil_Out32(SLCR_BASE + SLCR_UNLOCK_OFFSET, SLCR_UNLOCK_KEY);

    Xil_Out32(SLCR_BASE + SLCR_PSS_RST_CTRL_OFF, SLCR_SOFT_RST_BIT);
    __asm__ volatile("dsb sy");

    for (;;) { __asm__ volatile("nop"); }
}

/* Needs IRQs enabled in CPSR, else the ISR never runs. */

/* One writer each, so volatile is enough */
static volatile XTime g_hb_main = 0;
static volatile XTime g_hb_lwip = 0;
static volatile XTime g_hb_tx   = 0;
static volatile XTime g_hb_rx   = 0;

static volatile uint8_t g_main_section = WDT_SEC_NONE;

void wdt_section_mark(wdt_section_t s) { g_main_section = (uint8_t)s; }
uint8_t wdt_section_get(void) { return g_main_section; }

const char *wdt_section_name(uint8_t s) {
    switch (s) {
    case WDT_SEC_NONE:        return "NONE";
    case WDT_SEC_LWIP_INPUT:  return "LWIP_INPUT";
    case WDT_SEC_LWIP_TMR:    return "LWIP_TMR";
    case WDT_SEC_FIFO_DEB:    return "FIFO_DEB";
    case WDT_SEC_RADIO_POLL:  return "RADIO_POLL";
    case WDT_SEC_STATUS_SEND: return "STATUS_SEND";
    case WDT_SEC_EXEC_FUNC:   return "EXEC_FUNC";
    case WDT_SEC_AD9361_SPI:  return "AD9361_SPI";
    case WDT_SEC_VID_SEND:    return "VID_SEND";
    case WDT_SEC_OTHER:       return "OTHER";
    case WDT_SEC_AD9361_TEMP: return "AD9361_TEMP";
    case WDT_SEC_XADC:        return "XADC";
    case WDT_SEC_SPI_RSSI:    return "SPI_RSSI";
    case WDT_SEC_RADIO_TX:    return "RADIO_TX";
    case WDT_SEC_POST_LWIP:   return "POST_LWIP";
    default:                  return "?";
    }
}

/* COUNTS_PER_SECOND = CPU_FREQ/2 */
static inline uint64_t ms_to_ticks(uint32_t ms) {
    return ((uint64_t)COUNTS_PER_SECOND * (uint64_t)ms) / 1000ULL;
}
static inline uint32_t ticks_to_ms(uint64_t ticks) {
    return (uint32_t)(ticks / (COUNTS_PER_SECOND / 1000ULL));
}

static void wdt_isr(void *arg)
{
    XScuWdt *wdt = (XScuWdt *)arg;

    XScuWdt_WriteReg(wdt->Config.BaseAddr,
                     XSCUWDT_ISR_OFFSET,
                     XSCUWDT_ISR_EVENT_FLAG_MASK);

    XTime now;
    XTime_GetTime(&now);

    /* 0=MAIN 1=LWIP 2=TX 3=RX */
    uint32_t hb_age_ms[4];
    hb_age_ms[0] = ticks_to_ms((uint64_t)(now - g_hb_main));
    hb_age_ms[1] = ticks_to_ms((uint64_t)(now - g_hb_lwip));
    hb_age_ms[2] = ticks_to_ms((uint64_t)(now - g_hb_tx));
    hb_age_ms[3] = ticks_to_ms((uint64_t)(now - g_hb_rx));

    uint32_t starve_mask = 0;
    if (hb_age_ms[0] > WDT_HB_MAIN_MAX_MS) starve_mask |= WDT_HB_BIT_MAIN;
    if (hb_age_ms[1] > WDT_HB_LWIP_MAX_MS) starve_mask |= WDT_HB_BIT_LWIP;
    if (hb_age_ms[2] > WDT_HB_TX_MAX_MS  ) starve_mask |= WDT_HB_BIT_TX;
    if (hb_age_ms[3] > WDT_HB_RX_MAX_MS  ) starve_mask |= WDT_HB_BIT_RX;

    uint8_t oldest_idx = 0;
    uint32_t oldest_age = hb_age_ms[0];
    for (int i = 1; i < 4; i++) {
        if (hb_age_ms[i] > oldest_age) {
            oldest_age = hb_age_ms[i];
            oldest_idx = (uint8_t)i;
        }
    }

    uint8_t section = g_main_section;
    if (section >= WDT_SEC_MAX) section = WDT_SEC_OTHER;

    static const char *hb_names[4] = { "MAIN", "LWIP", "TX", "RX" };

    xil_printf("\r\n[WDT] TIMEOUT  sect=%s  oldest=%s age=%lums  starve=0x%X\r\n",
               wdt_section_name(section),
               hb_names[oldest_idx],
               (unsigned long)oldest_age,
               (unsigned)starve_mask);
    xil_printf("[WDT] HB ages: MAIN=%lu LWIP=%lu TX=%lu RX=%lu (ms)\r\n",
               (unsigned long)hb_age_ms[0], (unsigned long)hb_age_ms[1],
               (unsigned long)hb_age_ms[2], (unsigned long)hb_age_ms[3]);

    wdt_write_crash_to_slcr(section, oldest_idx, (uint8_t)starve_mask);

    wdt_trigger_soft_reset();
}

static volatile uint32_t g_stats_reboot_status = 0;
static volatile uint32_t g_stats_wdt_reset_flag = 0;
static volatile uint32_t g_stats_kick_count = 0;
static volatile uint32_t g_stats_starve_count = 0;
static volatile uint32_t g_stats_last_starve_mask = 0;
static XTime             g_start_time = 0;

/* valid=0 if our flag was not set */
static volatile uint8_t  g_stats_crash_section     = 0;
static volatile uint8_t  g_stats_crash_oldest_hb   = 0;
static volatile uint8_t  g_stats_crash_starve_mask = 0;
static volatile uint8_t  g_stats_crash_valid       = 0;

void wdt_capture_reboot_status(void)
{
    uint32_t st = Xil_In32(SLCR_BASE + SLCR_REBOOT_STATUS_OFF);
    g_stats_reboot_status = st;

    /* SLC_RST alone does not count: that may be a user reboot */
    int magic = (st & SLCR_WDT_MAGIC_BIT) ? 1 : 0;
    int hw_bits = (st & (SLCR_BIT_AWDT0_RST | SLCR_BIT_SWDT_RST)) ? 1 : 0;
    g_stats_wdt_reset_flag = (magic || hw_bits) ? 1 : 0;

    if (magic) {
        g_stats_crash_section     = (uint8_t)((st & SLCR_CRASH_SECTION_MASK) >> SLCR_CRASH_SECTION_SHIFT);
        g_stats_crash_oldest_hb   = (uint8_t)((st & SLCR_CRASH_OLDEST_MASK)  >> SLCR_CRASH_OLDEST_SHIFT);
        g_stats_crash_starve_mask = (uint8_t)(st & SLCR_CRASH_STARVE_MASK);
        g_stats_crash_valid       = 1;
    } else {
        g_stats_crash_section     = WDT_SEC_NONE;
        g_stats_crash_oldest_hb   = 0xF;
        g_stats_crash_starve_mask = 0;
        g_stats_crash_valid       = 0;
    }

    /* sticky bits: clear for the next reset */
    Xil_Out32(SLCR_BASE + SLCR_UNLOCK_OFFSET, SLCR_UNLOCK_KEY);
    Xil_Out32(SLCR_BASE + SLCR_REBOOT_STATUS_OFF,
              st & ~(SLCR_REASON_MASK | SLCR_CRASH_INFO_BITS));
    Xil_Out32(SLCR_BASE + SLCR_LOCK_OFFSET, SLCR_LOCK_KEY);

    if (g_stats_crash_valid) {
        static const char *hb_names[5] = { "MAIN", "LWIP", "TX", "RX", "?" };
        uint8_t oh = g_stats_crash_oldest_hb < 4 ? g_stats_crash_oldest_hb : 4;
        xil_printf("[WDT] REBOOT_STATUS=0x%08X (WDT MAGIC)  sect=%s  oldest=%s  starve=0x%X\r\n",
                   st,
                   wdt_section_name(g_stats_crash_section),
                   hb_names[oh],
                   (unsigned)g_stats_crash_starve_mask);
    } else {
        xil_printf("[WDT] REBOOT_STATUS=0x%08X%s\r\n",
                   st, hw_bits ? " (AWDT/SWDT HW bit, no crash-info)" : "");
    }
}

int wdt_init(struct XScuGic *gic)
{
    if (!gic) {
        xil_printf("[WDT] init: gic NULL\r\n");
        return -3;
    }

    XScuWdt_Config *cfg = XScuWdt_LookupConfig(XPAR_SCUWDT_0_DEVICE_ID);
    if (!cfg) {
        xil_printf("[WDT] LookupConfig failed\r\n");
        return -1;
    }

    int rc = XScuWdt_CfgInitialize(&g_wdt, cfg, cfg->BaseAddr);
    if (rc != XST_SUCCESS) {
        xil_printf("[WDT] CfgInitialize failed: %d\r\n", rc);
        return -2;
    }

    /* load = PERIPHCLK/(prescaler+1) * timeout_ms / 1000 */
    uint64_t ticks_per_sec_wdt = (uint64_t)COUNTS_PER_SECOND / (uint64_t)(WDT_PRESCALER + 1);
    uint64_t load64 = (ticks_per_sec_wdt * (uint64_t)WDT_TIMEOUT_MS) / 1000ULL;
    if (load64 > 0xFFFFFFFFULL) load64 = 0xFFFFFFFFULL;
    g_wdt_load_value = (uint32_t)load64;

    /* TIMER mode: watchdog mode resets only the CPU, we need the whole PS */
    uint32_t ctrl = 0;
    ctrl |= ((uint32_t)WDT_PRESCALER << 8) & XSCUWDT_CONTROL_PRESCALER_MASK;
    ctrl |= XSCUWDT_CONTROL_IT_ENABLE_MASK;
    XScuWdt_SetControlReg(&g_wdt, ctrl);

    XScuWdt_WriteReg(g_wdt.Config.BaseAddr,
                     XSCUWDT_ISR_OFFSET,
                     XSCUWDT_ISR_EVENT_FLAG_MASK);

    /* SCU WDT IRQ: PPI, ID 30 */
    XScuGic *p_gic = (XScuGic *)gic;
    rc = XScuGic_Connect(p_gic, XPAR_SCUWDT_INTR,
                         (Xil_ExceptionHandler)wdt_isr,
                         (void *)&g_wdt);
    if (rc != XST_SUCCESS) {
        xil_printf("[WDT] XScuGic_Connect failed: %d\r\n", rc);
        return -4;
    }
    XScuGic_Enable(p_gic, XPAR_SCUWDT_INTR);

    XScuWdt_LoadWdt(&g_wdt, g_wdt_load_value);

    g_wdt_inited = 1;
    xil_printf("[WDT] init OK: timeout=%u ms, load=0x%08lX, prescaler=%u, IRQ=%u\r\n",
               (unsigned)WDT_TIMEOUT_MS, (unsigned long)g_wdt_load_value,
               WDT_PRESCALER, (unsigned)XPAR_SCUWDT_INTR);
    return 0;
}

void wdt_start(void)
{
    if (!g_wdt_inited) {
        xil_printf("[WDT] start: not inited\r\n");
        return;
    }

    XTime_GetTime(&g_start_time);
    XTime now = g_start_time;
    g_hb_main = now;
    g_hb_lwip = now;
    g_hb_tx   = now;
    g_hb_rx   = now;

    XScuWdt_LoadWdt(&g_wdt, g_wdt_load_value);
    XScuWdt_Start(&g_wdt);
    g_wdt_started = 1;
    xil_printf("[WDT] started\r\n");
}

void wdt_stop(void)
{
    if (!g_wdt_inited) return;
    XScuWdt_Stop(&g_wdt);
    g_wdt_started = 0;
    xil_printf("[WDT] stopped\r\n");
}

void wdt_set_enabled(int enable)
{
    if (enable) {
        if (g_wdt_inited && !g_wdt_started) wdt_start();
    } else {
        if (g_wdt_started) wdt_stop();
    }
    xil_printf("[WDT] %s by request\r\n", enable ? "ENABLED" : "DISABLED");
}

int wdt_is_enabled(void) { return g_wdt_started; }

void wdt_kick(void)
{
    if (!g_wdt_started) return;

    XTime now;
    XTime_GetTime(&now);

    uint32_t starve_mask = 0;

#if WDT_CHECK_MAIN
    if (ticks_to_ms((uint64_t)(now - g_hb_main)) > WDT_HB_MAIN_MAX_MS)
        starve_mask |= WDT_HB_BIT_MAIN;
#endif
#if WDT_CHECK_LWIP
    if (ticks_to_ms((uint64_t)(now - g_hb_lwip)) > WDT_HB_LWIP_MAX_MS)
        starve_mask |= WDT_HB_BIT_LWIP;
#endif
#if WDT_CHECK_TX
    if (ticks_to_ms((uint64_t)(now - g_hb_tx)) > WDT_HB_TX_MAX_MS)
        starve_mask |= WDT_HB_BIT_TX;
#endif
#if WDT_CHECK_RX
    if (ticks_to_ms((uint64_t)(now - g_hb_rx)) > WDT_HB_RX_MAX_MS)
        starve_mask |= WDT_HB_BIT_RX;
#endif

    if (starve_mask == 0) {
        XScuWdt_RestartWdt(&g_wdt);
        g_stats_kick_count++;
    } else {
        g_stats_starve_count++;
        g_stats_last_starve_mask = starve_mask;
        static XTime last_log = 0;
        if (ticks_to_ms((uint64_t)(now - last_log)) > 1000u) {
            xil_printf("[WDT] STARVE mask=0x%02X — will reset soon\r\n",
                       (unsigned)starve_mask);
            last_log = now;
        }
    }
}

static volatile uint32_t g_progress_kick_calls = 0;

uint32_t wdt_get_progress_kick_calls(void) { return g_progress_kick_calls; }

void wdt_progress_kick(void)
{
    g_progress_kick_calls++;   /* before the throttle */
    if (!g_wdt_started) return;

    XTime now;
    XTime_GetTime(&now);

    static XTime last = 0;
    if (ticks_to_ms((uint64_t)(now - last)) < WDT_PROGRESS_KICK_MS) return;
    last = now;

    g_hb_main = now;
    wdt_kick();
}

void wdt_hb_main(void) { XTime t; XTime_GetTime(&t); g_hb_main = t; }
void wdt_hb_lwip(void) { XTime t; XTime_GetTime(&t); g_hb_lwip = t; }
void wdt_hb_tx  (void) { XTime t; XTime_GetTime(&t); g_hb_tx   = t; }
void wdt_hb_rx  (void) { XTime t; XTime_GetTime(&t); g_hb_rx   = t; }

void wdt_get_stats(wdt_stats_t *out)
{
    if (!out) return;
    out->reboot_status     = g_stats_reboot_status;
    out->wdt_reset_flag    = g_stats_wdt_reset_flag;
    out->kick_count        = g_stats_kick_count;
    out->starve_count      = g_stats_starve_count;
    out->last_starve_mask  = g_stats_last_starve_mask;
    out->crash_section     = g_stats_crash_section;
    out->crash_oldest_hb   = g_stats_crash_oldest_hb;
    out->crash_starve_mask = g_stats_crash_starve_mask;
    out->crash_valid       = g_stats_crash_valid;

    XTime now;
    XTime_GetTime(&now);
    out->uptime_sec = (uint32_t)((uint64_t)(now - g_start_time) / (uint64_t)COUNTS_PER_SECOND);
}

/* -DWDT_DIAG_HANG prints WDT/GIC regs while waiting */
void wdt_simulate_hang(void)
{
#ifdef WDT_DIAG_HANG
    const uint32_t WDT_BASE      = 0xF8F00620u;
    const uint32_t WDT_LOAD_OFF  = 0x00u;
    const uint32_t WDT_COUNT_OFF = 0x04u;
    const uint32_t WDT_CTRL_OFF  = 0x08u;
    const uint32_t WDT_ISR_OFF   = 0x0Cu;

    const uint32_t GIC_DIST_BASE      = 0xF8F01000u;
    const uint32_t GIC_ISENABLER0_OFF = 0x100u;
    const uint32_t GIC_ICPENDR0_OFF   = 0x280u;
    const uint32_t GIC_CPU_BASE       = 0xF8F00100u;
    const uint32_t GIC_PMR_OFF        = 0x04u;

    uint32_t cpsr_val = 0;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr_val));

    uint32_t ctrl  = Xil_In32(WDT_BASE + WDT_CTRL_OFF);
    uint32_t load  = Xil_In32(WDT_BASE + WDT_LOAD_OFF);
    uint32_t count = Xil_In32(WDT_BASE + WDT_COUNT_OFF);
    uint32_t isr_f = Xil_In32(WDT_BASE + WDT_ISR_OFF);
    uint32_t isenb = Xil_In32(GIC_DIST_BASE + GIC_ISENABLER0_OFF);
    uint32_t pmr   = Xil_In32(GIC_CPU_BASE + GIC_PMR_OFF);

    xil_printf("[WDT] === HANG DIAG START ===\r\n");
    xil_printf("[WDT] CPSR=0x%08X (I-bit=%u)\r\n",
               (unsigned)cpsr_val, (unsigned)((cpsr_val >> 7) & 1u));
    xil_printf("[WDT] CTRL=0x%08X (WD_ENABLE=%u, IT_ENABLE=%u, WD_MODE=%u, prescaler=%u)\r\n",
               (unsigned)ctrl,
               (unsigned)(ctrl & 1u),
               (unsigned)((ctrl >> 2) & 1u),
               (unsigned)((ctrl >> 3) & 1u),
               (unsigned)((ctrl >> 8) & 0xFFu));
    xil_printf("[WDT] LOAD=0x%08X  COUNT=0x%08X  ISR_FLAG=%u\r\n",
               (unsigned)load, (unsigned)count, (unsigned)(isr_f & 1u));
    xil_printf("[WDT] GIC ISENABLER0=0x%08X (bit30=%u)  PMR=0x%02X\r\n",
               (unsigned)isenb, (unsigned)((isenb >> 30) & 1u),
               (unsigned)(pmr & 0xFFu));
    xil_printf("[WDT] simulating hang — waiting for counter to drain...\r\n");

    XTime t_last;
    XTime_GetTime(&t_last);
    uint32_t tick = 0;
    for (;;) {
        XTime now;
        XTime_GetTime(&now);
        if (ticks_to_ms((uint64_t)(now - t_last)) >= 200u) {
            t_last = now;
            uint32_t c = Xil_In32(WDT_BASE + WDT_COUNT_OFF);
            uint32_t f = Xil_In32(WDT_BASE + WDT_ISR_OFF);
            uint32_t p = Xil_In32(GIC_DIST_BASE + GIC_ICPENDR0_OFF);
            xil_printf("[WDT] t=%lu ms  COUNT=0x%08X  ISR_FLAG=%u  GIC_PEND[30]=%u\r\n",
                       (unsigned long)(tick * 200u),
                       (unsigned)c, (unsigned)(f & 1u),
                       (unsigned)((p >> 30) & 1u));
            if (++tick > 30) {
                xil_printf("[WDT] === HANG DIAG TIMEOUT — handler did not run ===\r\n");
                for (;;) __asm__ volatile("nop");
            }
        }
        __asm__ volatile("nop");
    }
#else
    xil_printf("[WDT] simulating hang — system should reset within ~%u ms\r\n",
               (unsigned)WDT_TIMEOUT_MS);
    for (;;) {
        __asm__ volatile("nop");
    }
#endif
}

void wdt_force_reset_now(void)
{
    xil_printf("[WDT] force_reset_now — triggering SLCR.PSS_RST_CTRL directly\r\n");
    wdt_write_crash_to_slcr(WDT_SEC_NONE, 0xF, 0);
    wdt_trigger_soft_reset();
}

#endif /* !WDT_DISABLED */
