#include "modem.h"
#include "lwip/stats.h"
#include "xil_printf.h"
#include <stdlib.h>
#include <string.h>

extern struct ppos_tx tx_chan0;
extern struct ppos_rx rx_chan0;
extern struct ad9361_rf_phy *ad9361_phy;
extern struct ppos_par ppos;

extern  struct udp_pcb *debug_pcb;
extern  struct netif bridge_netif;

struct stat_modem* stat_modem_init()
{
    struct stat_modem* status_modem;
    status_modem = malloc(sizeof(struct stat_modem));
    if (status_modem == NULL) return NULL;
    /* is_clr from malloc is garbage: with anything but 0 or 1 no branch
     * fires and the stats stay empty forever. 1 = start counting now. */
    memset(status_modem, 0, sizeof(struct stat_modem));
    status_modem->stat_rx.is_clr    = 1;
    status_modem->stat_cntrl.is_clr = 1;
    return status_modem;
}


/* Needs LWIP_STATS, MEM_STATS, MEMP_STATS, LINK_STATS in lwipopts.h */
void modem_fill_stats(stats_resp_t *resp)
{
    if (resp == NULL) return;

    resp->head[0] = CMD_PROTO_MAGIC;
    resp->head[1] = CMD_GET_STATS;

#if LWIP_STATS && MEM_STATS
    resp->mem_used = (uint32_t)lwip_stats.mem.used;
    resp->mem_max  = (uint32_t)lwip_stats.mem.max;
    resp->mem_err  = (uint32_t)lwip_stats.mem.err;
#else
    resp->mem_used = resp->mem_max = resp->mem_err = 0;
#endif

#if LWIP_STATS && MEMP_STATS
    const struct stats_mem *pbuf_s = lwip_stats.memp[MEMP_PBUF];
    if (pbuf_s) {
        resp->pbuf_used = (uint32_t)pbuf_s->used;
        resp->pbuf_max  = (uint32_t)pbuf_s->max;
        resp->pbuf_err  = (uint32_t)pbuf_s->err;
    } else {
        resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    }

    const struct stats_mem *pool_s = lwip_stats.memp[MEMP_PBUF_POOL];
    if (pool_s) {
        resp->pool_used = (uint32_t)pool_s->used;
        resp->pool_max  = (uint32_t)pool_s->max;
        resp->pool_err  = (uint32_t)pool_s->err;
    } else {
        resp->pool_used = resp->pool_max = resp->pool_err = 0;
    }
#else
    resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    resp->pool_used = resp->pool_max = resp->pool_err = 0;
#endif

#if LWIP_STATS && LINK_STATS
    resp->link_drop = (uint32_t)lwip_stats.link.drop;
#else
    resp->link_drop = 0;
#endif
}

void modem_print_stats(void)
{
    stats_resp_t resp;
    modem_fill_stats(&resp);

    xil_printf("=== lwIP stats ===\r\n");
    xil_printf("  Heap:      used=%u  max=%u  err=%u\r\n",
               resp.mem_used, resp.mem_max, resp.mem_err);
    xil_printf("  PBUF hdr:  used=%u  max=%u  err=%u\r\n",
               resp.pbuf_used, resp.pbuf_max, resp.pbuf_err);
    xil_printf("  PBUF pool: used=%u  max=%u  err=%u\r\n",
               resp.pool_used, resp.pool_max, resp.pool_err);
    xil_printf("  Link drop: %u\r\n", resp.link_drop);
}


/* ---- SPI engine ---- */



void spi_engine_ad9361_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_AD9361_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 0;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;



    struct no_os_spi_init_param no_os_spi_init= {0};;
    no_os_spi_init.extra = &spi_engine_init_par;
    no_os_spi_init.mode=1;
    no_os_spi_init.device_id = 0;
    no_os_spi_init.chip_select = 0;
    no_os_spi_init.max_speed_hz = 50000000;


    spi_engine_init(spi_eng, &no_os_spi_init);

    return;
}



#if NPU_CELL_NEW

/* NPU cell: one MAX7301 (DD9) for panel power, indicators and the
 * preselector, plus an SPI-I2C bridge to eight INA226. */

#define MAX7301_REG_CONFIG      0x04   /* D0: 1 = normal, 0 = shutdown     */
#define MAX7301_REG_CFG_P7_P4   0x09   /* конфигурация портов, по 2 бита   */
#define MAX7301_REG_CFG_P11_P8  0x0A
#define MAX7301_REG_CFG_P15_P12 0x0B
#define MAX7301_REG_CFG_P19_P16 0x0C
#define MAX7301_REG_CFG_P23_P20 0x0D
#define MAX7301_REG_CFG_P27_P24 0x0E
#define MAX7301_REG_CFG_P31_P28 0x0F

/* Group write of 8 ports from P4: addr = 0x40 + first port, so 0x44.
 * The protocol says 0x40, which does not exist on a MAX7301. */
#define MAX7301_REG_P4_P11      0x44
#define MAX7301_REG_P14         0x2E   /* одиночный порт:  0x20 + номер    */
#define MAX7301_REG_P23_P30     0x57   /* группа 8 портов, D0 -> P23       */
#define MAX7301_REG_P31         0x5F

#define MAX7301_REG_P6_P13      0x46   /* индикаторы передающей панели N1  */
#define MAX7301_REG_P15_P22     0x4F   /* индикаторы передающей панели N2  */

#define MAX7301_READ            0x80   /* старший бит адреса = чтение      */


/* Адреса одиночных портов питания панелей, индекс — com_dev_type */
static const uint8_t npu_panel_reg[6] = {
    0x2E,   /* TX_PANEL_1 -> P14, EN1 */
    0x37,   /* TX_PANEL_2 -> P23, EN2 */
    0x38,   /* RX_PANEL_1 -> P24, EN3 */
    0x39,   /* RX_PANEL_2 -> P25, EN4 */
    0x3A,   /* RX_PANEL_3 -> P26, EN5 */
    0x3B    /* RX_PANEL_4 -> P27, EN6 */
};


static void max7301_write(struct no_os_spi_desc* spi_eng, uint8_t reg, uint8_t val)
{
    uint8_t tx_buf[2];
    tx_buf[0] = reg;
    tx_buf[1] = val;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    usleep(100);
}


/* Two frames: address, then a No-Op that shifts the data out */
static uint8_t max7301_read(struct no_os_spi_desc* spi_eng, uint8_t reg)
{
    uint8_t tx_buf[2];

    tx_buf[0] = MAX7301_READ | reg;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    /* MAX7301 needs tens of ns of CS high, 2 us is plenty */
    usleep(2);

    tx_buf[0] = 0x00;                 /* No-Op */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    return tx_buf[1];
}


/* Write with read-back, printed to UART: shows which register went
 * silent instead of just dead outputs. 0 = plain write. */
#define MAX7301_VERIFY_INIT  1

/* strict = 1 only for config regs 0x09..0x0F and 0x04: they keep what
 * was written. Port data regs read the pin, not the latch. */
static int max7301_wr_v(struct no_os_spi_desc* spi_eng,
                        uint8_t reg, uint8_t val,
                        uint8_t strict, const char* what)
{
    max7301_write(spi_eng, reg, val);

#if MAX7301_VERIFY_INIT
    uint8_t rb = max7301_read(spi_eng, reg);

    if (strict) {
        if (rb == val) {
            xil_printf("  [OK]   0x%02X <- 0x%02X            : %s\r\n",
                       reg, val, what);
            return 0;
        }
        xil_printf("  [FAIL] 0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
                   reg, val, rb, what);
        return -1;
    }

    xil_printf("  [WR]   0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
               reg, val, rb, what);
    return 0;
#else

    (void)reg; (void)val; (void)strict; (void)what;
    return 0;
#endif
}


/* SC18IS604 bridge (DD11) to eight INA226. Own chip select; the engine
 * drops exactly one CS per frame, so only one chip is ever selected. */
#if NPU_BRIDGE_DEBUG

/* bridge commands */
#define BRIDGE_CMD_WR_I2C       0x00   /* запись N байт в устройство I2C  */
#define BRIDGE_CMD_RD_I2C       0x01   /* чтение N байт из устройства     */
#define BRIDGE_CMD_RD_AFTER_WR  0x02   /* запись, повторный старт, чтение */
#define BRIDGE_CMD_RD_BUF       0x06   /* забрать принятое из буфера      */
#define BRIDGE_CMD_WR_REG       0x20   /* запись внутреннего регистра     */
#define BRIDGE_CMD_RD_REG       0x21   /* чтение внутреннего регистра     */
#define BRIDGE_CMD_VERSION      0xFE   /* строка версии в буфер чтения    */

/* bridge registers */
#define BRIDGE_REG_IOCONFIG     0x00
#define BRIDGE_REG_IOSTATE      0x01   /* уровни GPIO0..GPIO4             */
#define BRIDGE_REG_I2CCLOCK     0x02   /* частота = 1,875 МГц / N         */
#define BRIDGE_REG_I2CTO        0x03   /* таймаут, бит 0 — разрешение     */
#define BRIDGE_REG_I2CSTAT      0x04
#define BRIDGE_REG_I2CADR       0x05

/* I2CStat codes */
#define BRIDGE_STAT_OK          0xF0
#define BRIDGE_STAT_NACK_ADR    0xF1
#define BRIDGE_STAT_NACK_DAT    0xF2
#define BRIDGE_STAT_BUSY        0xF3
#define BRIDGE_STAT_TIMEOUT     0xF8
#define BRIDGE_STAT_BADCOUNT    0xF9

/* The bridge starts shifting data one byte after the command, so every
 * read sends (data + lag) bytes and takes the answer at that offset. */
#define BRIDGE_RD_LAG           1

/* 375 kHz: bridge limit, and INA226 takes 400 kHz */
#define BRIDGE_I2C_CLK_DIV      5
/* bus timeout: bits 7:1 value, bit 0 enable. Off after reset, and
 * without it a stuck device hangs the bridge for good. */
#define BRIDGE_I2C_TIMEOUT      0x7F
/* Bounded on purpose: unlimited waiting on this bus already cost us
 * reboots. 130 us per transaction, 50 us between polls. */
#define BRIDGE_POLL_MAX         30

/* INA226 addresses, consecutive */
#define INA226_ADR_FIRST        0x40
#define INA226_ADR_LAST         0x47

#define INA226_REG_SHUNT_V      0x01   /* знаковый, 2,5 мкВ на разряд     */
#define INA226_REG_BUS_V        0x02   /* 1,25 мВ на разряд               */
#define INA226_REG_DIE_ID       0xFF   /* должен читаться как 0x2260      */

static struct no_os_spi_desc* spi_eng_bridge  = NULL;
#if !NPU_BRIDGE_OWN_CS
/* only for the CS1 edges that bound the bridge frame */
static struct no_os_spi_desc* spi_eng_npu_ref = NULL;
#endif


#if !NPU_BRIDGE_OWN_CS
/* No-Op to the max, for the CS1 edge that raises CS2 */
static void max7301_noop(struct no_os_spi_desc* spi_eng)
{
    uint8_t tx_buf[2];
    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
}
#endif  /* !NPU_BRIDGE_OWN_CS */


/* Full duplex buffer: what went out on MOSI comes back in place */
static void bridge_xfer(uint8_t* buf, uint16_t len)
{
    if (!spi_eng_bridge)
        return;

#if NPU_BRIDGE_OWN_CS
    /* own CS: the engine bounds the frame itself */
    spi_engine_write_and_read(spi_eng_bridge, buf, len);
#else
    if (!spi_eng_npu_ref)
        return;

    max7301_noop(spi_eng_npu_ref);                      /* кадр открыт  */
    spi_engine_write_and_read(spi_eng_bridge, buf, len);
    max7301_noop(spi_eng_npu_ref);                      /* кадр закрыт  */
#endif
}


/* 0x21, register number, dummy byte; answer in the third byte */
static uint8_t bridge_read_reg(uint8_t reg)
{
    uint8_t buf[3 + BRIDGE_RD_LAG];
    int i;

    for (i = 0; i < 3 + BRIDGE_RD_LAG; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_REG;
    buf[1] = reg;
    bridge_xfer(buf, 3 + BRIDGE_RD_LAG);
    return buf[2 + BRIDGE_RD_LAG];
}


static void bridge_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[3];
    buf[0] = BRIDGE_CMD_WR_REG;
    buf[1] = reg;
    buf[2] = val;
    bridge_xfer(buf, 3);
}


/* Returns I2CStat, or BUSY if it never cleared. The read also clears INT. */
static uint8_t bridge_wait_done(void)
{
    uint8_t st = BRIDGE_STAT_BUSY;

    for (int i = 0; i < BRIDGE_POLL_MAX; ++i) {
        st = bridge_read_reg(BRIDGE_REG_I2CSTAT);
        if (st != BRIDGE_STAT_BUSY)
            return st;
        usleep(50);
    }
    return st;
}


/* One read buffer in the bridge, so strictly one transaction at a time:
 * command, wait, fetch. Returns 0, or an I2CStat code. */
static int ina226_read(uint8_t i2c_adr, uint8_t reg, uint16_t* val)
{
    uint8_t buf[6];
    uint8_t rb[3 + BRIDGE_RD_LAG];
    uint8_t st;
    int i;

    buf[0] = BRIDGE_CMD_RD_AFTER_WR;
    buf[1] = 1;                                 /* писать: 1 байт        */
    buf[2] = 2;                                 /* читать: 2 байта       */
    buf[3] = (uint8_t)(i2c_adr << 1);           /* адрес + признак записи */
    buf[4] = reg;                               /* указатель регистра    */
    buf[5] = (uint8_t)((i2c_adr << 1) | 1);     /* адрес + признак чтения */
    bridge_xfer(buf, 6);

    st = bridge_wait_done();
    if (st != BRIDGE_STAT_OK)
        return (int)st;

    for (i = 0; i < 3 + BRIDGE_RD_LAG; ++i)
        rb[i] = 0x00;
    rb[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(rb, 3 + BRIDGE_RD_LAG);

    *val = (uint16_t)(((uint16_t)rb[1 + BRIDGE_RD_LAG] << 8)
                      | rb[2 + BRIDGE_RD_LAG]);          /* старшим вперёд */
    return 0;
}


static const char* bridge_stat_name(uint8_t st)
{
    switch (st) {
    case BRIDGE_STAT_OK:       return "успешно";
    case BRIDGE_STAT_NACK_ADR: return "адрес не подтверждён";
    case BRIDGE_STAT_NACK_DAT: return "байт не подтверждён";
    case BRIDGE_STAT_BUSY:     return "занят, ожидание истекло";
    case BRIDGE_STAT_TIMEOUT:  return "таймаут I2C";
    case BRIDGE_STAT_BADCOUNT: return "неверное число байт";
    default:                   return "неизвестный код";
    }
}


/* Create before the first MAX7301 writes: spi_engine_init resets the
 * core. Frame params are reloaded per exchange, so two descriptors on
 * one core coexist without switching. */
static void bridge_desc_init(struct no_os_spi_desc* spi_eng_max)
{
    struct spi_engine_init_param spi_engine_init_par;
    struct no_os_spi_init_param  no_os_spi_init = {0};

#if NPU_BRIDGE_OWN_CS
    (void)spi_eng_max;
#else
    spi_eng_npu_ref = spi_eng_max;
#endif

    spi_engine_init_par.spi_engine_baseaddr = SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type           = SPI_ENGINE;
    spi_engine_init_par.data_width     = 8;
    spi_engine_init_par.cs_delay       = 0;
    spi_engine_init_par.ref_clk_hz     = 100000000;
    spi_engine_init_par.sdo_idle_state = 0;

    no_os_spi_init.extra        = &spi_engine_init_par;
    no_os_spi_init.mode         = NO_OS_SPI_MODE_3;  /* у моста другого нет */
    no_os_spi_init.device_id    = 1;
    /* cs2 is index 0 */
    no_os_spi_init.chip_select  = NPU_BRIDGE_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(&spi_eng_bridge, &no_os_spi_init);
    /* bridge tops out at 1.2 Mbit/s */
    spi_engine_set_speed(spi_eng_bridge, 1000000);
}


/* Startup probe, prints to UART and keeps nothing */
static void bridge_debug(void)
{
    uint8_t buf[17];
    int i;

    xil_printf("\r\n=== Мост SPI->I2C SC18IS604 (DD11) ===\r\n");

    /* 0xFE returns a known version string without touching I2C, so it
     * checks the SPI params and shows the buffer lag */
    buf[0] = BRIDGE_CMD_VERSION;
    bridge_xfer(buf, 1);

    for (i = 0; i < 17; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(buf, 17);

    xil_printf("  версия, hex :");
    for (i = 1 + BRIDGE_RD_LAG; i < 17; ++i)
        xil_printf(" %02X", buf[i]);
    xil_printf("\r\n  версия, txt : ");
    for (i = 1 + BRIDGE_RD_LAG; i < 17 && buf[i] >= 0x20 && buf[i] < 0x7F; ++i)
        xil_printf("%c", buf[i]);
    xil_printf("\r\n  ожидалось   : 53 43 31 38 49 53 36 30 34 ... \"SC18IS604 ...\"\r\n");

    /* read back: also proves 0x20 and 0x21 work both ways */
    bridge_write_reg(BRIDGE_REG_I2CCLOCK, BRIDGE_I2C_CLK_DIV);
    bridge_write_reg(BRIDGE_REG_I2CTO,    BRIDGE_I2C_TIMEOUT);

    xil_printf("  I2CClock <- 0x%02X, прочитано 0x%02X   (375 кГц)\r\n",
               BRIDGE_I2C_CLK_DIV, bridge_read_reg(BRIDGE_REG_I2CCLOCK));
    xil_printf("  I2CTO    <- 0x%02X, прочитано 0x%02X   (таймаут шины включён)\r\n",
               BRIDGE_I2C_TIMEOUT, bridge_read_reg(BRIDGE_REG_I2CTO));
    xil_printf("  IOSTATE   = 0x%02X                   (GPIO0..GPIO4 — ALERT от INA226)\r\n",
               bridge_read_reg(BRIDGE_REG_IOSTATE));

    /* Die ID checks the whole chain: SPI, I2C and the device */
    for (uint8_t a = INA226_ADR_FIRST; a <= INA226_ADR_LAST; ++a) {
        uint16_t id = 0, bus = 0;
        uint16_t sh = 0;
        int rc;

        rc = ina226_read(a, INA226_REG_DIE_ID, &id);
        if (rc != 0) {
            xil_printf("  INA226 0x%02X : нет ответа, I2CStat 0x%02X — %s\r\n",
                       a, (uint8_t)rc, bridge_stat_name((uint8_t)rc));
            continue;
        }

        ina226_read(a, INA226_REG_BUS_V,   &bus);
        ina226_read(a, INA226_REG_SHUNT_V, &sh);

        /* bus 1.25 mV/LSB, shunt 2.5 uV/LSB -> mA = count / 80 at 0.2 Ohm.
         * ⚠ shunt limit +-81.92 mV = only +-410 mA at 0.2 Ohm. */
        xil_printf("  INA226 0x%02X : ID 0x%04X, шина %d мВ, ток %d мА\r\n",
                   a, id,
                   (int)(((int)bus * 5) / 4),
                   (int)((int)(int16_t)sh / 80));
    }

    xil_printf("=== мост: конец разбора ===\r\n\r\n");
}


/* On request from the GUI (0x90 0x19). No printing: the result goes to
 * a cache and leaves with the next request. */
struct bridge_probe bridge_probe_last;

void spi_engine_bridge_probe(void)
{
    uint8_t buf[10];
    int i;

    for (i = 0; i < 10; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_VERSION;
    bridge_xfer(buf, 1);

    for (i = 0; i < 10; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(buf, 9 + BRIDGE_RD_LAG);

    for (i = 0; i < 8; ++i)
        bridge_probe_last.ver[i] = buf[i + 1 + BRIDGE_RD_LAG];

    bridge_probe_last.i2cclk = bridge_read_reg(BRIDGE_REG_I2CCLOCK);
    bridge_probe_last.cnt++;
}


/* Sweep of eight meters on request (0x90 0x1A). Die ID first as a gate,
 * so zeros are never taken for readings. Raw registers go up, the GUI
 * does the scaling because the divider depends on the shunt. */
struct npu_power npu_pwr;

void spi_engine_bridge_sweep(void)
{
    uint8_t a;
    int i;

    for (a = INA226_ADR_FIRST, i = 0; a <= INA226_ADR_LAST; ++a, ++i) {
        uint16_t id = 0, bus = 0, sh = 0;
        int rc;

        npu_pwr.bus[i]   = 0;
        npu_pwr.shunt[i] = 0;

        rc = ina226_read(a, INA226_REG_DIE_ID, &id);
        if (rc != 0) {
            npu_pwr.stat[i] = (uint8_t)rc;
            continue;
        }
        if (id != 0x2260) {
            npu_pwr.stat[i] = 0xFE;
            continue;
        }

        ina226_read(a, INA226_REG_BUS_V,   &bus);
        ina226_read(a, INA226_REG_SHUNT_V, &sh);

        npu_pwr.stat[i]  = 0x00;
        npu_pwr.bus[i]   = bus;
        npu_pwr.shunt[i] = (int16_t)sh;
    }

    npu_pwr.cnt++;
}
#endif  /* NPU_BRIDGE_DEBUG */



void spi_engine_npu_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    /* MOSI level between frames */
    spi_engine_init_par.sdo_idle_state = 0;

    struct no_os_spi_init_param no_os_spi_init = {0};
    no_os_spi_init.extra = &spi_engine_init_par;
#if NPU_SPI_MODE3
    /* Mode 3 on the whole bus: the bridge has no other */
    no_os_spi_init.mode = NO_OS_SPI_MODE_3;
#else
    no_os_spi_init.mode = NO_OS_SPI_MODE_0;
#endif
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select = NPU_BOARD_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    /* 1 MHz, clk_div = 49. Do not go above 25 MHz: 26 gives clk_div = 0,
     * which is 50 MHz, and the MAX7301 limit is 26. */
    spi_engine_set_speed(*spi_eng, 1000000);

#if NPU_BRIDGE_DEBUG
    /* before the MAX7301 setup: spi_engine_init resets the core */
    bridge_desc_init(*spi_eng);
#endif

    int errs = 0;

    xil_printf("\r\n=== MAX7301 init: ячейка НПУ (один CS) ===\r\n");

    /* Port config, 2 bits each: 00 unused, 01 out, 10 in, 11 in+pullup */
    max7301_write(*spi_eng, 0x00, 0x00);                        /* No-Op */

    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P7_P4,   0xF5, 1, "P7,P6 вход; P5,P4 выход (V2,V1)") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P11_P8,  0xFF, 1, "P11..P8  вход с подтяжкой") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P15_P12, 0xDF, 1, "P14 выход (EN1), прочие вход") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P19_P16, 0xFF, 1, "P19..P16 вход с подтяжкой") != 0);
    /* the protocol says 0x0B7F, a typo: P23..P20 config is in 0x0D */
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P23_P20, 0x7F, 1, "P23 выход (EN2), P22..P20 вход") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P27_P24, 0x55, 1, "P27..P24 выход (EN6..EN3)") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P31_P28, 0x5F, 1, "P31,P30 выход; P29,P28 вход") != 0);

    /* Written before leaving shutdown, so panel power does not blink.
     * No read-back check: in shutdown the ports are still inputs. */
    max7301_wr_v(*spi_eng, MAX7301_REG_P4_P11,  0x03, 0, "V2,V1 = 1,1 — без фильтра");
    max7301_wr_v(*spi_eng, MAX7301_REG_P14,     0x00, 0, "EN1 = 0");
    max7301_wr_v(*spi_eng, MAX7301_REG_P23_P30, 0x00, 0, "EN2..EN6 = 0, RES2 = 0");
    max7301_wr_v(*spi_eng, MAX7301_REG_P31,     0x01, 0, "RES3 = 1 (по протоколу)");

    /* 0x04 reading back as 0x01 means the cell answers and is awake */
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CONFIG, 0x01, 1, "выход из shutdown") != 0);

#if MAX7301_VERIFY_INIT
    if (errs == 0)
        xil_printf("=== ячейка НПУ: OK, ошибок сверки нет ===\r\n\r\n");
    else
        xil_printf("=== ячейка НПУ: ОШИБОК СВЕРКИ %d ===\r\n\r\n", errs);
#else
    (void)errs;
#endif

#if NPU_BRIDGE_DEBUG
    bridge_debug();
#endif

    return;
}


void spi_engine_npu_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{
    /* D0..D3 overheat 1..4, D4..D7 power 1..4; same order as stat_npu */
    status_npu->tx_panel_1.word = max7301_read(spi_eng, MAX7301_REG_P6_P13);
    status_npu->tx_panel_2.word = max7301_read(spi_eng, MAX7301_REG_P15_P22);
}


void spi_engine_npu_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{
    if ((unsigned int)com_dev > (unsigned int)RX_PANEL_4)
        return;

    max7301_write(spi_eng, npu_panel_reg[com_dev], (ena == DISABLED) ? 0x00 : 0x01);
}


void spi_engine_npu_set_f_diap(struct no_os_spi_desc*  spi_eng, pow_cntrl_type typ)
{
    /* 0x44: group write from P4. D0 -> V1, D1 -> V2. Codes: 0 = 4000-4200,
     * 1 = 3700-3980, 2 = 3400-3600, 3 = bypass. */
    max7301_write(spi_eng, MAX7301_REG_P4_P11, (uint8_t)(typ & 0x03));
}


/* V1 on P4, V2 on P5: written as a group, read one by one */
#define MAX7301_REG_P4          0x24
#define MAX7301_REG_P5          0x25


void spi_engine_npu_get_outputs(struct no_os_spi_desc*  spi_eng, struct npu_outputs* out)
{
    out->ena     = 0;
    out->f_diap  = 0;
    out->reserve = 0;

    /* config register first: if the cell is silent, skip the rest */
    out->cfg = max7301_read(spi_eng, MAX7301_REG_CONFIG);
    if (!(out->cfg & 0x01))
        return;

    for (uint8_t i = 0; i < 6; ++i) {
        if (max7301_read(spi_eng, npu_panel_reg[i]) & 0x01)
            out->ena |= (uint8_t)(1u << i);
    }

    /* reassembled as {V2,V1}, directly comparable to pow_cntrl_type */
    uint8_t v1 = max7301_read(spi_eng, MAX7301_REG_P4) & 0x01;
    uint8_t v2 = max7301_read(spi_eng, MAX7301_REG_P5) & 0x01;
    out->f_diap = (uint8_t)((v2 << 1) | v1);
}

#else   /* NPU_CELL_NEW */

/* Old NPU cell: one MAX7301 on a single chip select. Kept as a known
 * working branch for the old board. */

void spi_engine_npu_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;


    struct no_os_spi_init_param no_os_spi_init={0};
    no_os_spi_init.extra = &spi_engine_init_par;
    no_os_spi_init.mode=NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select =  NPU_BOARD_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    /* 1 MHz, clk_div = 49 */
    spi_engine_set_speed(*spi_eng, 1000000);
    uint8_t tx_buf[2];

    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x09;    tx_buf[1] = 0xff;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0A;    tx_buf[1] = 0xff;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0B;    tx_buf[1] = 0xfd;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0C;    tx_buf[1] = 0xff;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0D;    tx_buf[1] = 0x57;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0E;    tx_buf[1] = 0xD5;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0F;    tx_buf[1] = 0x5D;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x2c;    tx_buf[1] = 0x00;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);



    tx_buf[0] = 0x04;    tx_buf[1] = 0x01;    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);




    return;


}



/* Two frames: address, then No-Op. A single frame reads the echo of the
 * previous exchange, not the register. */
static uint8_t max7301_read(struct no_os_spi_desc* spi_eng, uint8_t reg)
{
    uint8_t tx_buf[2];

    tx_buf[0] = (uint8_t)(0x80 | reg);   /* кадр 1: адрес, бит 15 = чтение */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    usleep(2);   /* см. комментарий в ветке новой ячейки */

    tx_buf[0] = 0x00;                    /* кадр 2: No-Op, выталкивает данные */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    return tx_buf[1];
}


/* Group reads 0x44 (P4..P11) and 0x4D (P13..P20). The read bit is added
 * by max7301_read. D0..D3 overheat, D4..D7 power. */
void spi_engine_npu_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{
    status_npu->tx_panel_1.word = max7301_read(spi_eng, 0x44);
    status_npu->tx_panel_2.word = max7301_read(spi_eng, 0x4D);
}


 void spi_engine_npu_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{

   uint8_t tx_buf[2];
   if ( com_dev ==TX_PANEL_1 )   {
		tx_buf[0] = 0x2C;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==TX_PANEL_2 )   {
		tx_buf[0] = 0x35;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_1 )   {
		tx_buf[0] = 0x36;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_2 )   {
		tx_buf[0] = 0x37;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_3 )   {
		tx_buf[0] = 0x38;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_4 )   {
		tx_buf[0] = 0x39;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

}




/* Preselector on single ports P26 (0x3A) and P28 (0x3C) */
void spi_engine_npu_set_f_diap(struct no_os_spi_desc*  spi_eng, pow_cntrl_type typ)
{
    uint8_t tx_buf[2];

    uint8_t typ_1 = (typ & 0x02) >> 1;
    uint8_t typ_0 =  typ & 0x01;

    tx_buf[0] = 0x3A;
    tx_buf[1] = typ_1;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    tx_buf[0] = 0x3C;
    tx_buf[1] = typ_0;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
}




/* single port registers, in com_dev_type order */
static const uint8_t npu_panel_reg_old[6] = {
    0x2C,   /* TX_PANEL_1 -> P12 */
    0x35,   /* TX_PANEL_2 -> P21 */
    0x36,   /* RX_PANEL_1 -> P22 */
    0x37,   /* RX_PANEL_2 -> P23 */
    0x38,   /* RX_PANEL_3 -> P24 */
    0x39    /* RX_PANEL_4 -> P25 */
};


void spi_engine_npu_get_outputs(struct no_os_spi_desc*  spi_eng, struct npu_outputs* out)
{
    out->ena     = 0;
    out->f_diap  = 0;
    out->reserve = 0;

    /* config register first: if the cell is silent, skip the rest */
    out->cfg = max7301_read(spi_eng, 0x04);
    if (!(out->cfg & 0x01))
        return;

    for (uint8_t i = 0; i < 6; ++i) {
        if (max7301_read(spi_eng, npu_panel_reg_old[i]) & 0x01)
            out->ena |= (uint8_t)(1u << i);
    }

    /* P26 is the high bit of the code, P28 the low one */
    uint8_t p26 = max7301_read(spi_eng, 0x3A) & 0x01;
    uint8_t p28 = max7301_read(spi_eng, 0x3C) & 0x01;
    out->f_diap = (uint8_t)((p26 << 1) | p28);
}


#endif  /* NPU_CELL_NEW */








/* First tangent, creg2.Tang bit 16, paired with Tang_avt bit 17.
 * ⚠ Always set the pair: with Tang_avt = 1 the value bit is ignored. */
void ppos_set_tangenta(tang_mode_t mode)
{
    /* >=, not ==: this line has no polarity, bit 16 is just a value */
    if (mode >= TANG_AUTO) {
        ppos.creg2.inside.Tang_avt = 1;
    } else {
        ppos.creg2.inside.Tang_avt = 0;
        ppos.creg2.inside.Tang     = (mode == TANG_TX) ? 1 : 0;
    }
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);
}

/* Second tangent, creg2.NPU_CTX_nCRX bit 22, paired with bit 23.
 * ⚠ In auto, bit 22 is polarity, not value. An equal pair cuts the
 * antenna off, so always set the pair. */
void ppos_set_cont_tang(tang_mode_t mode)
{
    if (mode >= TANG_AUTO) {
        /* 0 gives the opposite phase, which is the normal pair */
        ppos.creg2.inside.NPU_CTX_nCRX     = (mode == TANG_AUTO_INPHASE) ? 1 : 0;
        ppos.creg2.inside.NPU_CTX_nCRX_avt = 1;
    } else {
        ppos.creg2.inside.NPU_CTX_nCRX_avt = 0;
        ppos.creg2.inside.NPU_CTX_nCRX     = (mode == TANG_TX) ? 1 : 0;
    }
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);
}















 void modem_init()
 {




	 /* those creg3 fields are gone from the FPGA */

	 ppos.creg2.word = 0;

	 /* AB_GPIO1R..6R pin profile. Value comes from NPU_GPIO_PROFILE and
	  * goes together with NPU_BOARD_CS: the profile decides which pin
	  * carries CS, the index which CS the engine drops. */
     #ifdef  IS_BORT
	 ppos.creg2.inside.GPIOnR_TYPE = 2;
	 ppos.creg2.inside.Pit = 1;
	 ppos.creg2.inside.Fdiap = 0;
     #else
	 ppos.creg2.inside.GPIOnR_TYPE = NPU_GPIO_PROFILE;
     #endif

	 /* Tang: 0 = RX, 1 = TX. NPU goes to auto, bort stays manual and
	  * needs Tang_avt = 0, else the slot machine takes the line. */
	 ppos.creg2.inside.Tang = 0;
     #ifdef  IS_BORT
	 ppos.creg2.inside.Tang_avt = 0;
     #else
	 ppos.creg2.inside.Tang_avt = 1;
     #endif

	 /* Second tangent in auto too, in the opposite phase: only (1,0) and
	  * (0,1) are normal, an equal pair cuts the antenna off. */
	 ppos.creg2.inside.NPU_CTX_nCRX = 0;
	 ppos.creg2.inside.NPU_CTX_nCRX_avt = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);


     /* ⚠ knk_krt_bvp has 0 here for both roles. We keep 1, to match
      * apply_role_to_pl() in main.c. */
     ppos.creg3.word = 0;
     ppos.creg3.inside.AD9361_ENABLE =1;
	 ppos.creg3.inside.AD9361_TXnRX = 1;
     #ifdef IS_MASTER
	  ppos.creg3.inside.ENABLE_AUTO = 1;
	  ppos.creg3.inside.MasterNSlave = 1;
     #endif
     #ifdef IS_SLAVE
      ppos.creg3.inside.ENABLE_AUTO = 1;
      ppos.creg3.inside.MasterNSlave = 0;
     #endif
	 ppos.creg3.inside.TXnRX_AUTO = 1;
     ppos.creg3.inside.IS_TDMA = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 12, ppos.creg3.word);


	 ppos.creg4.inside.txnrx_setup = 5;
	 ppos.creg4.inside.txnrx_hold  = 25;


	 ppos.creg4.inside.vco_setup   =  25;

/* Segments in our window and in the peer's; the roles are mirrored */
#ifdef  IS_SLAVE
	 ppos.creg4.inside.num_seg    = 14;
	 ppos.creg4.inside.num_rx_seg = 1;
#else
	 ppos.creg4.inside.num_seg    = 1;
	 ppos.creg4.inside.num_rx_seg = 14;
#endif




	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 16, ppos.creg4.word);









	 ppos.creg5.inside.slot_dur = 3500;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 20, ppos.creg5.word);


     ppos.status_on = 1;
     IP4_ADDR(&ppos.status_ip, 192, 168, 250, 22);
     ppos.status_port = 5007;







	 tx_chan0.creg0.inside.bod = 2;
	 tx_chan0.creg0.inside.code = 2;
	 tx_chan0.creg0.inside.mod = 2;
	 tx_chan0.creg0.inside.mux_ad9361_din = 5;
	 tx_chan0.creg0.inside.pkt_len = 0;//544; //576;//4800;//576;
	 tx_chan0.creg0.inside.scr_on = 0;//1;
	 tx_chan0.creg0.inside.gain = 4;
	 tx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, tx_chan0.creg0.word);


  	 tx_chan0.creg2.inside.ad9361_tx_freq = 3500000;//kHz
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 2*4,  (u32) tx_chan0.creg2.word);

  	 uint64_t freq =  tx_chan0.creg2.word;
  	 freq = freq*1000;
  	 ad9361_set_tx_lo_freq (ad9361_phy, freq);


  	 tx_chan0.creg3.inside.ad9361_tx_atten = 0x000003ff&(30000/250); //mdB/250  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   Tx2
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 3*4,  (u32) tx_chan0.creg3.word);

     uint32_t atten =  tx_chan0.creg3.inside.ad9361_tx_atten;
     atten*=250;

  	 ad9361_set_tx_attenuation (ad9361_phy, 1, atten);



	 rx_chan0.creg0.inside.bod = 2;
	 rx_chan0.creg0.inside.code = 2;
	 rx_chan0.creg0.inside.mod = 2;
	 rx_chan0.creg0.inside.pkt_len = 0;// 544;//576;// 4800;//576;
	 rx_chan0.creg0.inside.scr_on = 0;//1;
	 rx_chan0.creg0.inside.rcos_filt_on = 1;
	 rx_chan0.creg0.inside.rot_on = 1;
	 rx_chan0.creg0.inside.freq_shift_on = 0;

	 rx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0, rx_chan0.creg0.word);


	 rx_chan0.creg0.inside.run = 1;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0, rx_chan0.creg0.word);



 	 rx_chan0.creg1.inside.ad9361_rx_freq = 3500000;
     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 1*4,  (u32) rx_chan0.creg1.word);

     freq =  rx_chan0.creg1.word;
     freq = freq*1000;
     ad9361_set_rx_lo_freq (ad9361_phy, freq);



     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG1_BASEADDR + 15*4, 0,  (u32) 0x00000001);





#ifdef IS_ZED_BOARD


	 ad9361_set_rx_lo_freq (ad9361_phy,3500040000);
	 ad9361_set_tx_lo_freq (ad9361_phy,3500040000);

#endif

 }






