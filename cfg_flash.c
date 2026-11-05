#include "cfg_flash.h"
#include "xparameters.h"
#include "xqspips.h"
#include "xil_printf.h"
#include <string.h>

#define QSPI_DEVICE_ID        XPAR_XQSPIPS_0_DEVICE_ID

#define FLASH_PAGE_SIZE       256u
#define FLASH_SECTOR_SIZE     0x10000u    /* erase unit, cmd 0xD8 */

/* In the first 16 MB bank (3-byte addr) and above BOOT.BIN. Keep it < 16 MB. */
#define CFG_FLASH_BASE        0x00F00000u
#define CFG_SLOT_A            (CFG_FLASH_BASE)
#define CFG_SLOT_B            (CFG_FLASH_BASE + FLASH_SECTOR_SIZE)

/* flash commands */
#define CMD_WRITE_ENABLE      0x06
#define CMD_SECTOR_ERASE      0xD8
#define CMD_PAGE_PROGRAM      0x02
#define CMD_READ              0x03   /* 3-byte addr, no dummy */
#define CMD_READ_STATUS       0x05
#define STATUS_WIP            0x01   /* write in progress */

/* cmd(1) + addr(3) */
#define CMD_ADDR_BYTES        4

#define CFG_MAGIC             0x50434647u  /* 'PCFG' */
#define CFG_VERSION           2u           /* v2: + radio_params */

struct __attribute__((packed)) cfg_record {
    uint32_t magic;
    uint16_t version;
    uint16_t size;                  /* cfg + radio */
    uint32_t seq;
    struct device_config cfg;
    struct radio_params  radio;
    uint32_t crc32;                 /* over all bytes before this field */
};

#define CFG_PAYLOAD_SIZE (sizeof(struct device_config) + sizeof(struct radio_params))

static XQspiPs qspi;
static int     qspi_ready = 0;

static u8 tx_buf[CMD_ADDR_BYTES + FLASH_PAGE_SIZE];
static u8 rx_buf[CMD_ADDR_BYTES + FLASH_PAGE_SIZE];

/* CRC32, reflected, poly 0xEDB88320 */
static uint32_t crc32_calc(const void *data, unsigned len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void flash_wait_ready(void)
{
    u8 tx[2] = { CMD_READ_STATUS, 0 };
    u8 rx[2] = { 0, 0 };
    do {
        XQspiPs_PolledTransfer(&qspi, tx, rx, 2);
    } while (rx[1] & STATUS_WIP);
}

static void flash_write_enable(void)
{
    u8 cmd = CMD_WRITE_ENABLE;
    XQspiPs_PolledTransfer(&qspi, &cmd, NULL, 1);
}

static int flash_read(u32 addr, void *dst, unsigned len)
{
    if (len > FLASH_PAGE_SIZE) return -1;
    memset(tx_buf, 0, CMD_ADDR_BYTES + len);
    tx_buf[0] = CMD_READ;
    tx_buf[1] = (addr >> 16) & 0xFF;
    tx_buf[2] = (addr >> 8) & 0xFF;
    tx_buf[3] = addr & 0xFF;
    if (XQspiPs_PolledTransfer(&qspi, tx_buf, rx_buf, CMD_ADDR_BYTES + len) != XST_SUCCESS)
        return -1;
    memcpy(dst, rx_buf + CMD_ADDR_BYTES, len);
    return 0;
}

static int flash_sector_erase(u32 addr)
{
    u8 tx[CMD_ADDR_BYTES];
    flash_write_enable();
    tx[0] = CMD_SECTOR_ERASE;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr & 0xFF;
    if (XQspiPs_PolledTransfer(&qspi, tx, NULL, CMD_ADDR_BYTES) != XST_SUCCESS)
        return -1;
    flash_wait_ready();
    return 0;
}

/* len <= 256, addr page-aligned */
static int flash_page_program(u32 addr, const void *src, unsigned len)
{
    if (len > FLASH_PAGE_SIZE) return -1;
    flash_write_enable();
    tx_buf[0] = CMD_PAGE_PROGRAM;
    tx_buf[1] = (addr >> 16) & 0xFF;
    tx_buf[2] = (addr >> 8) & 0xFF;
    tx_buf[3] = addr & 0xFF;
    memcpy(tx_buf + CMD_ADDR_BYTES, src, len);
    if (XQspiPs_PolledTransfer(&qspi, tx_buf, NULL, CMD_ADDR_BYTES + len) != XST_SUCCESS)
        return -1;
    flash_wait_ready();
    return 0;
}

static int record_valid(const struct cfg_record *r)
{
    if (r->magic != CFG_MAGIC)                  return 0;
    if (r->version != CFG_VERSION)              return 0;
    if (r->size != CFG_PAYLOAD_SIZE)            return 0;
    uint32_t crc = crc32_calc(r, sizeof(*r) - sizeof(r->crc32));
    return (crc == r->crc32);
}

static int slot_read(u32 addr, struct cfg_record *r)
{
    if (flash_read(addr, r, sizeof(*r)) != 0) return 0;
    return record_valid(r);
}

int cfg_flash_init(void)
{
    XQspiPs_Config *cfg = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
    if (cfg == NULL) {
        xil_printf("[QSPI] LookupConfig failed\r\n");
        return -1;
    }
    if (XQspiPs_CfgInitialize(&qspi, cfg, cfg->BaseAddress) != XST_SUCCESS) {
        xil_printf("[QSPI] CfgInitialize failed\r\n");
        return -1;
    }
    /* manual start + SW slave select: app access, not linear/XIP */
    XQspiPs_SetOptions(&qspi, XQSPIPS_MANUAL_START_OPTION |
                              XQSPIPS_FORCE_SSELECT_OPTION |
                              XQSPIPS_HOLD_B_DRIVE_OPTION);
    XQspiPs_SetClkPrescaler(&qspi, XQSPIPS_CLK_PRESCALE_8);
    XQspiPs_SetSlaveSelect(&qspi);

    qspi_ready = 1;
    xil_printf("[QSPI] init ok (cfg area @0x%08X, slots A/B)\r\n", CFG_FLASH_BASE);
    return 0;
}

int cfg_flash_load(struct device_config *net, struct radio_params *radio)
{
    if (!qspi_ready) return 0;

    struct cfg_record ra, rb;
    int va = slot_read(CFG_SLOT_A, &ra);
    int vb = slot_read(CFG_SLOT_B, &rb);

    const struct cfg_record *best = NULL;
    if (va && vb) best = (ra.seq >= rb.seq) ? &ra : &rb;
    else if (va)  best = &ra;
    else if (vb)  best = &rb;

    if (!best) {
        xil_printf("[QSPI] no valid config (A:%d B:%d) -> defaults\r\n", va, vb);
        return 0;
    }
    *net   = best->cfg;
    *radio = best->radio;
    xil_printf("[QSPI] loaded profile seq=%lu (slot %c): role=%d id=%d tx=%lu kHz\r\n",
               (unsigned long)best->seq, (best == &ra) ? 'A' : 'B',
               best->cfg.role, best->cfg.device_id,
               (unsigned long)best->radio.tx.creg2.word);
    return 1;
}

int cfg_flash_save(const struct device_config *net, const struct radio_params *radio)
{
    if (!qspi_ready) return -1;

    struct cfg_record ra, rb;
    int va = slot_read(CFG_SLOT_A, &ra);
    int vb = slot_read(CFG_SLOT_B, &rb);

    uint32_t maxseq = 0;
    if (va && ra.seq > maxseq) maxseq = ra.seq;
    if (vb && rb.seq > maxseq) maxseq = rb.seq;

    /* keep the newest slot, write the other one */
    u32 target;
    if (!va && !vb)                              target = CFG_SLOT_A;
    else if (va && (!vb || ra.seq >= rb.seq))     target = CFG_SLOT_B;
    else                                          target = CFG_SLOT_A;

    struct cfg_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic   = CFG_MAGIC;
    rec.version = CFG_VERSION;
    rec.size    = CFG_PAYLOAD_SIZE;
    rec.seq     = maxseq + 1;
    rec.cfg     = *net;
    rec.radio   = *radio;
    rec.crc32   = crc32_calc(&rec, sizeof(rec) - sizeof(rec.crc32));

    if (flash_sector_erase(target) != 0)               return -2;
    if (flash_page_program(target, &rec, sizeof(rec)))  return -3;

    /* read back and check */
    struct cfg_record back;
    if (!slot_read(target, &back) || back.seq != rec.seq) {
        xil_printf("[QSPI] save VERIFY FAILED (slot 0x%08X)\r\n", target);
        return -4;
    }
    xil_printf("[QSPI] saved profile seq=%lu (slot %c): role=%d id=%d tx=%lu kHz\r\n",
               (unsigned long)rec.seq, (target == CFG_SLOT_A) ? 'A' : 'B',
               net->role, net->device_id, (unsigned long)radio->tx.creg2.word);
    return 0;
}

int cfg_flash_erase(void)
{
    if (!qspi_ready) return -1;
    int rc = 0;
    if (flash_sector_erase(CFG_SLOT_A) != 0) rc = -2;
    if (flash_sector_erase(CFG_SLOT_B) != 0) rc = -2;
    xil_printf("[QSPI] config erased (factory reset) rc=%d\r\n", rc);
    return rc;
}
