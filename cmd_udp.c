/* ===================================================================
 * cmd_udp.c -- UDP command handler (protocol 0x90 + legacy ucnt)
 *
 * Build with -DLOG_DEBUG=1 (basic) or -DLOG_DEBUG=2 (verbose)
 * to enable debug output.  Default: silent (LOG_DEBUG=0).
 * =================================================================== */

#include "cmd_udp.h"
#include "platform_config.h"   /* FW_ROLE_ID / FW_BUILD_STR */
#include "radio_stream.h"
#include "modem.h"
#include "firewall.h"
#include "watchdog.h"
#include "bridge_func.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "xllfifo.h"
#include "xil_types.h"
#include <string.h>
#include <stdbool.h>

/* ---- external globals ---- */
extern struct netif             bridge_netif;
extern struct netif             port3_bridge;
extern XLlFifo                  fifo;
extern struct ad9361_rf_phy    *ad9361_phy;
extern struct no_os_spi_desc    desc_tmp;

extern struct ppos_tx  tx_chan0;
extern struct ppos_rx  rx_chan0;   /* в main.c определена как ppos_rx */
extern struct ppos_par ppos;

/* Runtime-управляемый уровень логирования. Меняется через 0x90 0x46.
 * volatile т.к. читается из main loop / hot path и может быть изменён
 * из cmd_udp_callback (другой контекст исполнения). */
volatile int g_log_level = LOG_DEBUG_DEFAULT;

/* Forward declaration — определение ниже по файлу. */
static void handle_creg_exch(struct udp_pcb *upcb, uint8_t *data,
                             uint16_t len, const ip_addr_t *addr, u16_t port);

/* ===================================================================
 *  Helpers
 * =================================================================== */

/* Allocate a PBUF_TRANSPORT, fill header [0x90, sub], return payload ptr.
 * Returns NULL on allocation failure.  Caller must pbuf_free(). */
static struct pbuf *resp_alloc(uint8_t sub, uint16_t total_len)
{
    struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);
    if (pb) {
        uint8_t *d = (uint8_t *)pb->payload;
        d[0] = 0x90;
        d[1] = sub;
    }
    return pb;
}

/* Send pbuf and free it. */
static void resp_send(struct udp_pcb *upcb, struct pbuf *pb,
                      const ip_addr_t *addr, u16_t port)
{
    udp_sendto(upcb, pb, addr, port);
    pbuf_free(pb);
}

/* Serialize uint32 to buf, little-endian. */
static inline void put_le32(uint8_t *buf, uint32_t v)
{
    buf[0] = (v)       & 0xFF;
    buf[1] = (v >> 8)  & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
}

/* Serialize uint64 to buf, little-endian. */
static inline void put_le64(uint8_t *buf, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        buf[i] = (v >> (i * 8)) & 0xFF;
}

/* Parse uint64 from buf, little-endian. */
static inline uint64_t get_le64(const uint8_t *buf)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)buf[i]) << (i * 8);
    return v;
}

/* Parse uint32 from buf, little-endian. */
static inline uint32_t get_le32(const uint8_t *buf)
{
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/* ===================================================================
 *  Protocol 0x90 handler
 * =================================================================== */
static void handle_proto_0x90(struct udp_pcb *upcb, uint8_t *data,
                              uint16_t len, const ip_addr_t *addr, u16_t port)
{
    uint8_t sub = data[1];
    LOG1("[CMD] 0x90 sub=0x%02X len=%d\r\n", sub, len);

    switch (sub) {

    /* ==================== WRITE commands ==================== */

    case 0x02: {  /* Set TX frequency */
        if (len != 10) { LOG1("[CMD] 0x02 bad len %d\r\n", len); break; }
        uint64_t freq = get_le64(&data[2]);
        LOG2("[CMD] TX freq parsed: %lu%lu Hz\r\n",
             (uint32_t)(freq >> 32), (uint32_t)(freq & 0xFFFFFFFF));
        if (freq < 70000000ULL || freq > 6000000000ULL) {
            LOG1("[CMD] TX freq out of range\r\n");
            break;
        }
        int32_t rc = ad9361_set_tx_lo_freq(ad9361_phy, freq);
        if (rc == 0) {
            uint64_t actual = 0;
            ad9361_get_tx_lo_freq(ad9361_phy, &actual);
            LOG2("[CMD] TX freq set OK, actual=%lu%lu\r\n",
                 (uint32_t)(actual >> 32), (uint32_t)(actual & 0xFFFFFFFF));
        } else {
            LOG1("[CMD] TX freq error %ld\r\n", rc);
        }
        break;
    }

    case 0x03: {  /* Set RX frequency */
        if (len != 10) { LOG1("[CMD] 0x03 bad len %d\r\n", len); break; }
        uint64_t freq = get_le64(&data[2]);
        LOG2("[CMD] RX freq parsed: %lu%lu Hz\r\n",
             (uint32_t)(freq >> 32), (uint32_t)(freq & 0xFFFFFFFF));
        if (freq < 70000000ULL || freq > 6000000000ULL) {
            LOG1("[CMD] RX freq out of range\r\n");
            break;
        }
        int32_t rc = ad9361_set_rx_lo_freq(ad9361_phy, freq);
        if (rc == 0) {
            uint64_t actual = 0;
            ad9361_get_rx_lo_freq(ad9361_phy, &actual);
            LOG2("[CMD] RX freq set OK, actual=%lu%lu\r\n",
                 (uint32_t)(actual >> 32), (uint32_t)(actual & 0xFFFFFFFF));
        } else {
            LOG1("[CMD] RX freq error %d\r\n", rc);
        }
        break;
    }

    case 0x04: {  /* Set TX attenuation */
        if (len != 6) { LOG1("[CMD] 0x04 bad len %d\r\n", len); break; }
        uint32_t atten_db = get_le32(&data[2]);
        if (atten_db > 890) { LOG1("[CMD] TX att out of range\r\n"); break; }
        uint32_t atten_mdb = atten_db * 100;
        int32_t rc = ad9361_set_tx_attenuation(ad9361_phy, 1, atten_mdb);
        LOG2("[CMD] TX att %lu dB -> rc=%ld\r\n", atten_db, rc);
        break;
    }

    case 0x05: {  /* Test generator */
        if (len != 7) { LOG1("[CMD] 0x05 bad len %d\r\n", len); break; }
        uint32_t fcode = get_le32(&data[2]);
        uint8_t mask = data[6];
        uint8_t d[4];
        d[0] = 0x84;
        d[1] = 0x18; d[2] = (fcode >> 24) & 0xFF;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        d[1] = 0x19; d[2] = (fcode >> 16) & 0xFF;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        d[1] = 0x1a; d[2] = (fcode >> 8) & 0xFF;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        d[1] = 0x1b; d[2] = fcode & 0xFF;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        d[1] = 0x00; d[2] = 0x08;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        d[1] = 0x01; d[2] = (mask) ? 0xC0 : 0x90;
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        LOG2("[CMD] Test gen fcode=0x%08lX mask=0x%02X\r\n", fcode, mask);
        break;
    }

    case 0x06: {  /* RX modulation */
        if (len != 3) { LOG1("[CMD] 0x06 bad len %d\r\n", len); break; }
        uint8_t d[4] = {0x84, 0x17, data[2], 0};
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        LOG2("[CMD] RX mod par[23] = 0x%02X\r\n", data[2]);
        break;
    }

    case 0x07: {  /* TX modulation */
        if (len != 3) { LOG1("[CMD] 0x07 bad len %d\r\n", len); break; }
        uint8_t d[4] = {0x84, 0x16, data[2], 0};
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        LOG2("[CMD] TX mod par[22] = 0x%02X\r\n", data[2]);
        break;
    }

    case 0x08: {  /* Set GPIO/UF control */
        if (len != 3) { LOG1("[CMD] 0x08 bad len %d\r\n", len); break; }
        uint8_t qt_mask = data[2];
        uint8_t gpio_val = 0;

        /* bit 0: uf_pit (power) */
        if (qt_mask & 0x01)
            gpio_val |= (1 << 0);

        /* bit 1: uf_tangenta (0=RX, 1=TX) -- from Qt bit3 */
        if (qt_mask & 0x08)
            gpio_val |= (1 << 1);

        /* bit 2: uf_sel (freq selector: 0=3.4-3.6 GHz, 1=3.7-3.9 GHz) */
        uint8_t freq_sel = (qt_mask >> 1) & 0x03;
        if (freq_sel == 1)
            gpio_val |= (1 << 2);

        /* bit 3: uf_tangenta_ena_avt -- reserved, zero */

        uint8_t d[4] = {0x84, 0x28, gpio_val, 0};
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);

        usleep(10);
        uint8_t r[4] = {0x85, 0x28, 0x00, 0x00};
        no_os_spi_write_and_read_11bit(&desc_tmp, r, 1);

        LOG2("[CMD] GPIO qt=0x%02X -> par[40]=0x%02X readback=0x%02X\r\n",
             qt_mask, gpio_val, r[2]);
        break;
    }

    case 0x14: {  /* AD9361 fastlock store */
        if (len != 3) { LOG1("[CMD] 0x14 bad len %d\r\n", len); break; }
        uint8_t val     = data[2];
        uint8_t profile = val & 0x07;
        uint8_t txnrx   = (val >> 6) & 0x01;
        ad9361_fastlock_store(ad9361_phy, (bool)txnrx, (uint32_t)profile);
        LOG2("[CMD] Fastlock store profile=%d txnrx=%d\r\n", profile, txnrx);
        break;
    }

    case 0x15: {  /* FHSS toggle */
        if (len != 3) { LOG1("[CMD] 0x15 bad len %d\r\n", len); break; }
        bool fhss_on = (data[2] == 0x01);
        uint8_t d[4];
        if (fhss_on) {
            d[0] = 0x82; d[1] = 0x5a; d[2] = 0x03; d[3] = 0;
            no_os_spi_write_and_read(&desc_tmp, d, 1);
            d[1] = 0x9a;
            no_os_spi_write_and_read(&desc_tmp, d, 1);
            d[0] = 0x84; d[1] = 0x2a; d[2] = 0x40;
            no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        } else {
            d[0] = 0x82; d[1] = 0x5a; d[2] = 0x00; d[3] = 0;
            no_os_spi_write_and_read(&desc_tmp, d, 1);
            d[1] = 0x9a;
            no_os_spi_write_and_read(&desc_tmp, d, 1);
            d[0] = 0x84; d[1] = 0x2a; d[2] = 0x00;
            no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        }
        LOG2("[CMD] FHSS %s\r\n", fhss_on ? "ON" : "OFF");
        break;
    }

    case 0x16: {  /* NPU panel enable/disable (via exec_func type=18) */
        if (len != 4) { LOG1("[CMD] 0x16 bad len %d\r\n", len); break; }
        /* data[2] = panel index (0=TX1,1=TX2,2=RX1,3=RX2,4=RX3,5=RX4)
         * data[3] = enable (0=DISABLE, 1=ENABLE) */
        extern u8 func_type;
        extern u8 func_par[16];
        extern bool func_start;
        func_type = 18;
        func_par[0] = data[2];  /* com_dev_type */
        func_par[1] = data[3];  /* ena_type */
        func_start = true;
        LOG2("[CMD] NPU panel=%d ena=%d\r\n", data[2], data[3]);
        break;
    }

    case 0x17: {  /* NPU preselector (via exec_func type=19) */
        if (len != 3) { LOG1("[CMD] 0x17 bad len %d\r\n", len); break; }
        /* data[2] = preselector index (0=4000-4200,1=3700-3980,2=3400-3600,3=BYPASS) */
        extern u8 func_type;
        extern u8 func_par[16];
        extern bool func_start;
        func_type = 19;
        func_par[0] = data[2];  /* pow_cntrl_type */
        func_start = true;
        LOG2("[CMD] NPU preselector=%d\r\n", data[2]);
        break;
    }

    /* ==================== READ commands ==================== */

    case 0x10: {  /* Read TX frequency */
        if (len != 2) break;
        uint64_t freq = 0;
        ad9361_get_tx_lo_freq(ad9361_phy, &freq);
        struct pbuf *pb = resp_alloc(0x10, 10);
        if (!pb) break;
        put_le64((uint8_t *)pb->payload + 2, freq);
        resp_send(upcb, pb, addr, port);
        LOG2("[CMD] Read TX freq -> %lu%lu\r\n",
             (uint32_t)(freq >> 32), (uint32_t)(freq & 0xFFFFFFFF));
        break;
    }

    case 0x11: {  /* Read RX frequency */
        if (len != 2) break;
        uint64_t freq = 0;
        ad9361_get_rx_lo_freq(ad9361_phy, &freq);
        struct pbuf *pb = resp_alloc(0x11, 10);
        if (!pb) break;
        put_le64((uint8_t *)pb->payload + 2, freq);
        resp_send(upcb, pb, addr, port);
        LOG2("[CMD] Read RX freq -> %lu%lu\r\n",
             (uint32_t)(freq >> 32), (uint32_t)(freq & 0xFFFFFFFF));
        break;
    }

    case 0x12: {  /* Read TX attenuation */
        if (len != 2) break;
        uint32_t atten_mdb = 0;
        ad9361_get_tx_attenuation(ad9361_phy, 1, &atten_mdb);
        uint32_t atten_db = atten_mdb / 100;
        struct pbuf *pb = resp_alloc(0x12, 6);
        if (!pb) break;
        put_le32((uint8_t *)pb->payload + 2, atten_db);
        resp_send(upcb, pb, addr, port);
        LOG2("[CMD] Read TX att -> %lu dB\r\n", atten_db);
        break;
    }

    case 0x13: {  /* Read FPGA register par[N] */
        if (len != 3) break;
        uint8_t par_addr = data[2];
        if (par_addr >= 42) { LOG1("[CMD] par addr %d > 41\r\n", par_addr); break; }
        uint8_t d[4] = {0x85, par_addr, 0x00, 0x00};
        no_os_spi_write_and_read_11bit(&desc_tmp, d, 1);
        struct pbuf *pb = resp_alloc(0x13, 4);
        if (!pb) break;
        uint8_t *r = (uint8_t *)pb->payload;
        r[2] = par_addr;
        r[3] = d[2];
        resp_send(upcb, pb, addr, port);
        LOG2("[CMD] Read par[%d] = 0x%02X\r\n", par_addr, d[2]);
        break;
    }

    /* ==================== Diagnostics ==================== */

    case 0x20: {  /* Statistics (radio_stream + lwIP) */
        #define STATS20_LEN 78
        struct pbuf *pb = resp_alloc(0x20, STATS20_LEN);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        memset(d + 2, 0, STATS20_LEN - 2);

        radio_stats_t rs;
        radio_stream_get_stats(&rs);
        int o = 2;
        #define S_PUT(val) do { put_le32(d + o, (val)); o += 4; } while(0)
        S_PUT(rs.tx_pkts);       S_PUT(rs.tx_blocks);
        S_PUT(rs.err_fifo_full); S_PUT(rs.rx_blocks);
        S_PUT(rs.err_seq_gap);   S_PUT(rs.err_rx_overflow);
        S_PUT(rs.rx_pkts);       S_PUT(rs.err_pkt_gap);
        S_PUT(rs.err_crc);

        stats_resp_t ls;
        modem_fill_stats(&ls);
        S_PUT(ls.mem_used);  S_PUT(ls.mem_max);  S_PUT(ls.mem_err);
        S_PUT(ls.pbuf_used); S_PUT(ls.pbuf_max); S_PUT(ls.pbuf_err);
        S_PUT(ls.pool_used); S_PUT(ls.pool_max); S_PUT(ls.pool_err);
        S_PUT(ls.link_drop);
        #undef S_PUT
        #undef STATS20_LEN
        resp_send(upcb, pb, addr, port);
        break;
    }

    case 0x21: {  /* Loopback get/set */
        if (len == 3)
            radio_stream_set_loopback(data[2] ? 1 : 0);
        struct pbuf *pb = resp_alloc(0x21, 3);
        if (!pb) break;
        ((uint8_t *)pb->payload)[2] = (uint8_t)radio_stream_get_loopback();
        resp_send(upcb, pb, addr, port);
        break;
    }

    case 0x22: {  /* Segmentation test -- send N frames */
        /* 1. Вытолкнуть bridge-остаток из tx_ring (иначе чужие pkt_id
         *    вылетят после тестовых → ложный gap).
         * 2. Сбросить ID-счётчики RX-парсера: первый пакет после
         *    bridge-трафика не даст gap с тестовыми фреймами. */
        radio_tx_periodic_flush();
        radio_stream_reset_rx_ids();

        uint8_t n_frames = (len >= 3 && data[2] > 0) ? data[2] : 1;
        if (n_frames > 16) n_frames = 16;
        static uint8_t test_seq = 0;
        uint8_t sent_ok = 0;

        for (int fi = 0; fi < n_frames; fi++) {
            struct pbuf *tp = pbuf_alloc(PBUF_RAW, 60, PBUF_RAM);
            if (!tp) break;
            uint8_t *eth = (uint8_t *)tp->payload;
            memset(eth, 0xFF, 6);
            eth[6] = 0x00; eth[7] = 0x0A; eth[8] = 0x35;
            eth[9] = 0xAA; eth[10] = 0xBB; eth[11] = 0xCC;
            eth[12] = 0x08; eth[13] = 0x00;
            for (int i = 14; i < 60; i++)
                eth[i] = (uint8_t)(test_seq + i);
            test_seq++;
            if (radio_linkoutput(&port3_bridge, tp) == ERR_OK)
                sent_ok++;
            pbuf_free(tp);
        }
        radio_tx_periodic_flush();
        #define TEST22_LEN 82
        struct pbuf *pb = resp_alloc(0x22, TEST22_LEN);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        memset(d + 2, 0, TEST22_LEN - 2);
        d[2] = sent_ok;
        d[3] = (uint8_t)radio_stream_get_loopback();

        int o = 4;
        #define S_PUT(val) do { put_le32(d + o, (val)); o += 4; } while(0)
        radio_stats_t rs;
        radio_stream_get_stats(&rs);
        S_PUT(rs.tx_pkts);       S_PUT(rs.tx_blocks);
        S_PUT(rs.err_fifo_full); S_PUT(rs.rx_blocks);
        S_PUT(rs.err_seq_gap);   S_PUT(rs.err_rx_overflow);
        S_PUT(rs.rx_pkts);       S_PUT(rs.err_pkt_gap);
        S_PUT(rs.err_crc);

        stats_resp_t ls;
        modem_fill_stats(&ls);
        S_PUT(ls.mem_used);  S_PUT(ls.mem_max);  S_PUT(ls.mem_err);
        S_PUT(ls.pbuf_used); S_PUT(ls.pbuf_max); S_PUT(ls.pbuf_err);
        S_PUT(ls.pool_used); S_PUT(ls.pool_max); S_PUT(ls.pool_err);
        S_PUT(ls.link_drop);
        #undef S_PUT
        #undef TEST22_LEN
        resp_send(upcb, pb, addr, port);
        break;
    }

    /* ==================== FIFO diagnostics ==================== */

    case 0x30: {  /* FIFO write -- test 188-byte block */
        static uint32_t fifo_write_count = 0;
        u32 vacancy = XLlFifo_iTxVacancy(&fifo);
        uint8_t ok = (vacancy >= (188 / 4)) ? 1 : 0;

        if (ok) {
            uint8_t block[188];
            block[0] = (uint8_t)(fifo_write_count & 0xFF);
            for (int i = 1; i < 188; i++)
                block[i] = (uint8_t)i;
            for (int i = 0; i < 47; i++) {
                u32 word = (u32)block[i*4]
                         | ((u32)block[i*4+1] <<  8)
                         | ((u32)block[i*4+2] << 16)
                         | ((u32)block[i*4+3] << 24);
                XLlFifo_TxPutWord(&fifo, word);
            }
            XLlFifo_iTxSetLen(&fifo, 188);
            fifo_write_count++;
            vacancy = XLlFifo_iTxVacancy(&fifo);
        }

        struct pbuf *pb = resp_alloc(0x30, 11);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        d[2] = ok;
        put_le32(d + 3, fifo_write_count);
        put_le32(d + 7, vacancy);
        resp_send(upcb, pb, addr, port);
        break;
    }

    case 0x31: {  /* FIFO status */
        u32 vac = XLlFifo_iTxVacancy(&fifo);
        u32 occ = XLlFifo_iRxOccupancy(&fifo);
        u32 isr = XLlFifo_IntPending(&fifo);
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);

        struct pbuf *pb = resp_alloc(0x31, 18);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        put_le32(d + 2,  vac);
        put_le32(d + 6,  occ);
        put_le32(d + 10, isr);
        memset(d + 14, 0, 4);
        resp_send(upcb, pb, addr, port);
        break;
    }

    case 0x32: {  /* FIFO reset */
        XLlFifo_Reset(&fifo);
        volatile int dly = 10000;
        while (dly--) {}
        u32 vac = XLlFifo_iTxVacancy(&fifo);

        struct pbuf *pb = resp_alloc(0x32, 3);
        if (!pb) break;
        ((uint8_t *)pb->payload)[2] = (vac > 0) ? 1 : 0;
        resp_send(upcb, pb, addr, port);
        break;
    }

    /* ==================== Watchdog diagnostics ==================== */

    case 0x40: {  /* Read watchdog stats
                   * Response payload after [0x90][0x40]:
                   *   [2..5]   reboot_status (SLCR)
                   *   [6]      wdt_reset_flag (0/1)
                   *   [7]      reserved
                   *   [8..11]  kick_count
                   *   [12..15] starve_count
                   *   [16..19] last_starve_mask
                   *   [20..23] uptime_sec
                   *   [24]     crash_section (wdt_section_t)
                   *   [25]     crash_oldest_hb (0=MAIN 1=LWIP 2=TX 3=RX 0xF=none)
                   *   [26]     crash_starve_mask
                   *   [27]     crash_valid (1 если crash-info прочиталась)
                   */
        wdt_stats_t st;
        wdt_get_stats(&st);
        struct pbuf *pb = resp_alloc(0x40, 28);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        put_le32(d + 2,  st.reboot_status);
        d[6] = (uint8_t)st.wdt_reset_flag;
        d[7] = 0;
        put_le32(d + 8,  st.kick_count);
        put_le32(d + 12, st.starve_count);
        put_le32(d + 16, st.last_starve_mask);
        put_le32(d + 20, st.uptime_sec);
        d[24] = st.crash_section;
        d[25] = st.crash_oldest_hb;
        d[26] = st.crash_starve_mask;
        d[27] = st.crash_valid;
        resp_send(upcb, pb, addr, port);
        LOG1("[CMD 0x40] WDT stats: uptime=%lu kick=%lu starve=%lu mask=0x%02X crash{sect=%s oldest=%u mask=0x%X valid=%u}\r\n",
             st.uptime_sec, st.kick_count, st.starve_count,
             (unsigned)st.last_starve_mask,
             wdt_section_name(st.crash_section),
             (unsigned)st.crash_oldest_hb,
             (unsigned)st.crash_starve_mask,
             (unsigned)st.crash_valid);
        break;
    }

    case 0x41: {  /* Simulate hang (для тестирования WDT) */
        if (len < 3 || data[2] != 0x5A) {
            LOG1("[CMD 0x41] bad magic\r\n");
            break;
        }
        struct pbuf *pb = resp_alloc(0x41, 3);
        if (pb) {
            ((uint8_t *)pb->payload)[2] = 0x01;
            resp_send(upcb, pb, addr, port);
        }
        LOG1("[CMD 0x41] simulating hang — system will reset\r\n");
        wdt_simulate_hang();
        break;
    }

    case 0x42: {  /* Direct SLCR soft reset (bypass WDT/ISR) */
        if (len < 3 || data[2] != 0xA5) {
            LOG1("[CMD 0x42] bad magic\r\n");
            break;
        }
        struct pbuf *pb = resp_alloc(0x42, 3);
        if (pb) {
            ((uint8_t *)pb->payload)[2] = 0x01;
            resp_send(upcb, pb, addr, port);
        }
        LOG1("[CMD 0x42] direct SLCR soft reset\r\n");
        wdt_force_reset_now();
        break;
    }

    /* ─── Recovery handlers (manual trigger, без PS reset) ──────────────
     * 0x43 — recover lwIP (cleanup dynamic FDB)
     * 0x44 — recover radio TX (FIFO TX reset + ring/counters)
     * 0x45 — recover radio RX (FIFO RX reset + parser state)
     * Все требуют magic byte 0x52 (= 'R') в payload[2]. */
    case 0x43: {  /* Recover lwIP */
        if (len < 3 || data[2] != 0x52) {
            LOG1("[CMD 0x43] bad magic\r\n");
            struct pbuf *pb = resp_alloc(0x43, 3);
            if (pb) { ((uint8_t *)pb->payload)[2] = 0x00; resp_send(upcb, pb, addr, port); }
            break;
        }
        u16_t cleared = bridge_recover_lwip();
        struct pbuf *pb = resp_alloc(0x43, 5);
        if (pb) {
            uint8_t *d = (uint8_t *)pb->payload;
            d[2] = 0x01;
            d[3] = (uint8_t)(cleared & 0xFF);
            d[4] = (uint8_t)((cleared >> 8) & 0xFF);
            resp_send(upcb, pb, addr, port);
        }
        LOG1("[CMD 0x43] lwip recovery: %u FDB entries cleared\r\n", (unsigned)cleared);
        break;
    }

    case 0x44: {  /* Recover radio TX */
        if (len < 3 || data[2] != 0x52) {
            LOG1("[CMD 0x44] bad magic\r\n");
            struct pbuf *pb = resp_alloc(0x44, 3);
            if (pb) { ((uint8_t *)pb->payload)[2] = 0x00; resp_send(upcb, pb, addr, port); }
            break;
        }
        radio_recover_tx();
        struct pbuf *pb = resp_alloc(0x44, 3);
        if (pb) { ((uint8_t *)pb->payload)[2] = 0x01; resp_send(upcb, pb, addr, port); }
        LOG1("[CMD 0x44] radio TX recovery done\r\n");
        break;
    }

    case 0x45: {  /* Recover radio RX */
        if (len < 3 || data[2] != 0x52) {
            LOG1("[CMD 0x45] bad magic\r\n");
            struct pbuf *pb = resp_alloc(0x45, 3);
            if (pb) { ((uint8_t *)pb->payload)[2] = 0x00; resp_send(upcb, pb, addr, port); }
            break;
        }
        radio_recover_rx();
        struct pbuf *pb = resp_alloc(0x45, 3);
        if (pb) { ((uint8_t *)pb->payload)[2] = 0x01; resp_send(upcb, pb, addr, port); }
        LOG1("[CMD 0x45] radio RX recovery done\r\n");
        break;
    }

    /* Get/Set IQ-stream enable (debug FIFO → UDP на PC).
     *   Request len=2: GET — ответ [0x90][0x47][state]
     *   Request len=3: SET — payload[2] = 0/1, ответ тот же.
     * По умолчанию OFF — иначе spam забивает pbuf pool и WDT-ребутит. */
    case 0x47: {
        extern volatile bool g_iq_stream_enabled;
        if (len >= 3) {
            g_iq_stream_enabled = (data[2] != 0);
            xil_printf("[IQ] stream %s\r\n", g_iq_stream_enabled ? "ENABLED" : "disabled");
        }
        struct pbuf *pb = resp_alloc(0x47, 3);
        if (pb) {
            ((uint8_t *)pb->payload)[2] = g_iq_stream_enabled ? 1 : 0;
            resp_send(upcb, pb, addr, port);
        }
        break;
    }

    /* Get/Set runtime log level (0..3).
     *   Request len=2: GET — ответ [0x90][0x46][level]
     *   Request len=3: SET — payload[2] = новый уровень, ответ тот же. */
    case 0x46: {
        if (len >= 3) {
            int new_level = (int)data[2];
            if (new_level < 0) new_level = 0;
            if (new_level > 3) new_level = 3;
            g_log_level = new_level;
            /* Печатаем безусловно (через xil_printf), чтобы видеть смену
             * уровня даже когда выключаем логи в 0. */
            xil_printf("[LOG] level set to %d\r\n", new_level);
        }
        struct pbuf *pb = resp_alloc(0x46, 3);
        if (pb) {
            ((uint8_t *)pb->payload)[2] = (uint8_t)g_log_level;
            resp_send(upcb, pb, addr, port);
        }
        break;
    }

    case 0x48: {  /* Firmware info: роль + версия (чтобы по eth убедиться
                   * ЧТО и КУДА залито).
                   * Response payload after [0x90][0x48]:
                   *   [2]    role_id (1=MASTER, 2=SLAVE, 0=мисконфиг)
                   *   [3]    reserved (0)
                   *   [4..]  ASCII версия, null-terminated (FW_BUILD_STR) */
        const char *ver = FW_BUILD_STR;
        uint16_t vlen = (uint16_t)strlen(ver);
        if (vlen > 200) vlen = 200;
        struct pbuf *pb = resp_alloc(0x48, (uint16_t)(4 + vlen + 1));
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        d[2] = (uint8_t)FW_ROLE_ID;
        d[3] = 0;
        memcpy(d + 4, ver, vlen);
        d[4 + vlen] = 0;
        resp_send(upcb, pb, addr, port);
        LOG1("[CMD 0x48] FW info: role_id=%u %s\r\n", (unsigned)FW_ROLE_ID, ver);
        break;
    }

    case 0x49: {  /* Инструментация залипания (диагностика мастера).
                   * Response payload after [0x90][0x49]:
                   *   [2..5]   xemacif_max_us  (пик с прошлого чтения, потом сброс)
                   *   [6..9]   xemacif_last_us (длительность последнего прохода)
                   *   [10..13] progress_kick_calls (вызовы wdt_progress_kick до троттла)
                   *   [14..17] kick_count (фактические перезарядки WDT-таймера) */
        extern volatile uint32_t g_xemacif_max_us, g_xemacif_last_us;
        wdt_stats_t wst;
        wdt_get_stats(&wst);
        struct pbuf *pb = resp_alloc(0x49, 18);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        put_le32(d + 2,  g_xemacif_max_us);
        put_le32(d + 6,  g_xemacif_last_us);
        put_le32(d + 10, wdt_get_progress_kick_calls());
        put_le32(d + 14, wst.kick_count);
        resp_send(upcb, pb, addr, port);
        g_xemacif_max_us = 0;   /* сброс пика → следующий запрос даёт max за интервал */
        break;
    }

    case 0x4A: {  /* A: get/set рейт-кап radio TX.
                   *   len=2: GET. len=6: SET (payload[2..5] = cap бит/с, LE32, 0=выкл).
                   * Response: [2..5]=cap_bps  [6..9]=ratecap_drops */
        if (len == 6) {
            uint32_t v = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                         ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
            g_radio_tx_cap_bps = v;
            xil_printf("[CAP] radio TX cap = %lu bps%s\r\n",
                       (unsigned long)v, v ? "" : " (выключен)");
        }
        struct pbuf *pb = resp_alloc(0x4A, 10);
        if (!pb) break;
        uint8_t *d = (uint8_t *)pb->payload;
        put_le32(d + 2, g_radio_tx_cap_bps);
        put_le32(d + 6, radio_get_tx_ratecap_drops());
        resp_send(upcb, pb, addr, port);
        break;
    }

    default:
        LOG1("[CMD] Unknown 0x90 sub=0x%02X\r\n", sub);
        break;
    }
}

/* ===================================================================
 *  Legacy protocol handler (ucnt[2] + cmd[1] + payload)
 * =================================================================== */
static void handle_legacy(struct udp_pcb *upcb, uint8_t *data,
                          uint16_t len, const ip_addr_t *addr, u16_t port)
{
    uint16_t ucnt_rx = ((uint16_t)data[0] << 8) | data[1];
    uint8_t  cmd     = data[2];

    LOG1("[CMD] Legacy cmd=0x%02X ucnt=%d len=%d\r\n", cmd, ucnt_rx, len);

    switch (cmd) {

    case 0x05: {  /* Set firewall config (11 bytes) */
        if (len < 11) break;
        uint32_t ip_mask = ((uint32_t)data[3] << 24)
                         | ((uint32_t)data[4] << 16)
                         | ((uint32_t)data[5] << 8)
                         | ((uint32_t)data[6]);
        firewall_set_config(ip_mask, data[7], data[8], data[9], data[10]);
        LOG1("[CMD 0x05] mask=%d.%d.%d.%d ARP=%d ICMP=%d TCP=%d UDP=%d\r\n",
             (ip_mask >> 24) & 0xFF, (ip_mask >> 16) & 0xFF,
             (ip_mask >> 8) & 0xFF,  ip_mask & 0xFF,
             data[7], data[8], data[9], data[10]);
        break;
    }

    case 0x06: {  /* Query firewall config -> respond 11 bytes */
        firewall_config_t cfg = firewall_get_config();
        struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 11, PBUF_RAM);
        if (!resp) break;
        uint8_t *r = (uint8_t *)resp->payload;
        r[0] = data[0];  /* echo ucnt */
        r[1] = data[1];
        r[2] = 0x06;
        r[3] = (cfg.ip_mask >> 24) & 0xFF;
        r[4] = (cfg.ip_mask >> 16) & 0xFF;
        r[5] = (cfg.ip_mask >> 8)  & 0xFF;
        r[6] =  cfg.ip_mask        & 0xFF;
        r[7]  = cfg.arp_en;
        r[8]  = cfg.icmp_en;
        r[9]  = cfg.tcp_en;
        r[10] = cfg.udp_en;
        resp_send(upcb, resp, addr, port);
        LOG1("[CMD 0x06] Firewall query from ucnt=%d -> responded\r\n", ucnt_rx);
        break;
    }

    case 0x07: {  /* Set IP via broadcast (15 bytes) */
        if (len < 15) break;
        uint32_t ip_val = ((uint32_t)data[3]  << 24) | ((uint32_t)data[4]  << 16)
                        | ((uint32_t)data[5]  << 8)  | ((uint32_t)data[6]);
        uint32_t nm_val = ((uint32_t)data[7]  << 24) | ((uint32_t)data[8]  << 16)
                        | ((uint32_t)data[9]  << 8)  | ((uint32_t)data[10]);
        uint32_t gw_val = ((uint32_t)data[11] << 24) | ((uint32_t)data[12] << 16)
                        | ((uint32_t)data[13] << 8)  | ((uint32_t)data[14]);

        ip4_addr_t new_ip, new_mask, new_gw;
        IP4_ADDR(&new_ip,   (ip_val >> 24) & 0xFF, (ip_val >> 16) & 0xFF,
                             (ip_val >> 8)  & 0xFF,  ip_val & 0xFF);
        IP4_ADDR(&new_mask, (nm_val >> 24) & 0xFF, (nm_val >> 16) & 0xFF,
                             (nm_val >> 8)  & 0xFF,  nm_val & 0xFF);
        IP4_ADDR(&new_gw,   (gw_val >> 24) & 0xFF, (gw_val >> 16) & 0xFF,
                             (gw_val >> 8)  & 0xFF,  gw_val & 0xFF);

        /* Отправляем ACK ДО смены IP — пакет уходит со старого адреса.
         * Формат ответа: ucnt[2] + 0x07 + ok[1] + new_ip[4] = 8 байт */
        struct pbuf *ack07 = pbuf_alloc(PBUF_TRANSPORT, 8, PBUF_RAM);
        if (ack07) {
            uint8_t *r = (uint8_t *)ack07->payload;
            r[0] = data[0]; r[1] = data[1];  /* echo ucnt */
            r[2] = 0x07;
            r[3] = 0x01;  /* OK */
            r[4] = (ip_val >> 24) & 0xFF;
            r[5] = (ip_val >> 16) & 0xFF;
            r[6] = (ip_val >>  8) & 0xFF;
            r[7] =  ip_val        & 0xFF;
            resp_send(upcb, ack07, addr, port);
        }

        netif_set_addr(&bridge_netif, &new_ip, &new_mask, &new_gw);

        LOG1("[CMD 0x07] IP=%d.%d.%d.%d mask=%d.%d.%d.%d gw=%d.%d.%d.%d\r\n",
             (ip_val >> 24) & 0xFF, (ip_val >> 16) & 0xFF,
             (ip_val >> 8)  & 0xFF,  ip_val & 0xFF,
             (nm_val >> 24) & 0xFF, (nm_val >> 16) & 0xFF,
             (nm_val >> 8)  & 0xFF,  nm_val & 0xFF,
             (gw_val >> 24) & 0xFF, (gw_val >> 16) & 0xFF,
             (gw_val >> 8)  & 0xFF,  gw_val & 0xFF);
        break;
    }

    default:
        LOG2("[CMD] Legacy unknown cmd=0x%02X\r\n", cmd);
        break;
    }
}

/* ===================================================================
 *  Main entry point -- UDP callback
 * =================================================================== */
void cmd_udp_callback(void *arg, struct udp_pcb *upcb,
                      struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (p == NULL)
        return;

    uint8_t *data = (uint8_t *)p->payload;
    uint16_t len  = p->len;

    LOG1("[CMD] pkt from %d.%d.%d.%d:%d len=%d\r\n",
         ip4_addr1(addr), ip4_addr2(addr),
         ip4_addr3(addr), ip4_addr4(addr), port, len);

    LOG2("[CMD] data[0]=0x%02X\r\n", data[0]);
      if (((len == 8)||(len == 4)) && data[0] == 0xA0)
    {
    	handle_creg_exch(upcb, data, len, addr, port);
    }
      else if (len >= 2 && data[0] == 0x90) {
        handle_proto_0x90(upcb, data, len, addr, port);
    } else if (len >= 3) {
        handle_legacy(upcb, data, len, addr, port);
    } else {
        LOG1("[CMD] Packet too short (%d bytes), ignored\r\n", len);
    }

    pbuf_free(p);
}



static void handle_creg_exch(struct udp_pcb *upcb, uint8_t *data,
                          uint16_t len, const ip_addr_t *addr, u16_t port)
{

    uint8_t group = data[1];
    uint8_t creg  = data[2];
    uint8_t type  = data[3];
    LOG1("[CMD] 0xA0 group=0x%02X creg=0x%02X type=0x%02X len=%d\r\n", group, creg, type, len);


    if (group == 0) //ppos
    {

    	 switch (type) {

    	      case 0x00: {  /* Set cregs */

    	        u32 offset = creg;
    	        u32 word  =  (u32)data[7]        | ((u32)data[6] << 8) |
    	                     ((u32)data[5] << 16)| ((u32)data[4] << 24);

    	        ppos.creg2.word = word;

    	        if (offset == 2)
    	        	{
    	        	 ppos.creg2.word = word;
    	        	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, offset*4,  (u32) ppos.creg2.word);
    	        	}

    	        if (offset == 3)
    	         	{
    	         	 ppos.creg3.word = word;
    	         	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, offset*4,  (u32) ppos.creg3.word);
    	         	}

    	        if (offset == 5)
    	      	    {
    	      	     ppos.creg5.word = word;
    	      	     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, offset*4,  (u32) ppos.creg5.word);
    	      	    }


    	          break;
    	        }
    	    }
    	    }






    if (group == 1) //tx_chan0
    {


     switch (type) {

      case 0x00: {  /* Set cregs */

        u32 offset = creg;
        u32 word  =  (u32)data[7]        | ((u32)data[6] << 8) |
                     ((u32)data[5] << 16)| ((u32)data[4] << 24);

        if (offset == 0)
        	{
        	 tx_chan0.creg0.word = word;
        	 tx_chan0.creg0.inside.run = 0;
        	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, offset*4,  (u32) tx_chan0.creg0.word);
        	}


        if (offset == 1)
        	{
        	 tx_chan0.creg1.word = word;
        	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, offset*4,  (u32) tx_chan0.creg1.word);
        	}



        if (offset == 2)
          	{
          	 tx_chan0.creg2.word = word;
          	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, offset*4,  (u32) tx_chan0.creg2.word);


          	 uint64_t freq =  tx_chan0.creg2.word;
          	 freq = freq*1000;
          	 ad9361_set_tx_lo_freq (ad9361_phy, freq);

          	}

        if (offset == 3)
          	{
          	 tx_chan0.creg3.inside.ad9361_tx_atten = 0x000003ff&word;
          	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, offset*4,  (u32) tx_chan0.creg3.word);

             uint32_t atten =  tx_chan0.creg3.inside.ad9361_tx_atten;
             atten*=250;

          	 ad9361_set_tx_attenuation (ad9361_phy, 1, atten);
          	}



            break;
        }
    }
    }



    if (group == 2) //rx_chan0
    {


     switch (type) {

      case 0x00: {  /* Set cregs */

        u32 offset = creg;
        u32 word  =  (u32)data[7]        | ((u32)data[6] << 8) |
                     ((u32)data[5] << 16)| ((u32)data[4] << 24);

        if (offset == 0)
        	{
        	 rx_chan0.creg0.word = word;
        	 rx_chan0.creg0.inside.run = 0;
        	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, offset, rx_chan0.creg0.word);
        	}


        if (offset == 1)
          	{
          	 rx_chan0.creg1.word = word;
          	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, offset*4,  (u32) rx_chan0.creg1.word);


          	 uint64_t freq =  rx_chan0.creg1.word;
          	 freq = freq*1000;
          	 ad9361_set_rx_lo_freq (ad9361_phy, freq);

          	}






            break;
        }
    }
    }



    if (group == 3) //
    {

     switch (type) {

      case 0x00: {  /* Set cregs */

        u32 offset = creg;
        u32 word  =  (u32)data[7]        | ((u32)data[6] << 8) |
                     ((u32)data[5] << 16)| ((u32)data[4] << 24);

        if (offset == 0)
        	{
        	   word = swap32(word);
        	   //memcpy((uint32_t*) &ppos.status_ip, &word, 4);
        	   memcpy((uint8_t*) &ppos.status_ip, (uint8_t*) &word, 4);

        	}

        if (offset == 1)
            {
        	   ppos.status_on = (word>>16)&0x000001;
        	   ppos.status_port = word&0x0000ffff;
            }




            break;
        }
    }
    }










}



void send_modem_status(struct udp_pcb *upcb, struct stat_modem* status)
{
    if (ppos.status_on)
    {

 	udp_bind_netif (upcb, &bridge_netif);

     struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct stat_modem), PBUF_RAM);
     if (!p) {
         xil_printf("Failed to allocate pbuf for deb packet\n\r");
         return;
     }

     memcpy((uint8_t*) p->payload, (uint8_t*) status, sizeof(struct stat_modem));

     err_t out = udp_sendto(upcb, p, &ppos.status_ip, ppos.status_port);

     if (out != ERR_OK) {
         /* ERR_MEM (-1) тут = lwIP без pbuf'ов под нагрузкой (backpressure
          * GEM-TX). Раньше печаталось безусловно и флудило UART в hot-ish path
          * (раз в ~5с, но десятки секунд подряд) — гейтим уровнем лога.
          * Сам факт виден в 0x20: mem_err / pool_err. */
         LOG1("Error udp_sendto status: %d\n\r", out);
     }

     pbuf_free(p);
    }
    return;
 }
















