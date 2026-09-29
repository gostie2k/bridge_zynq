#include "radio_params.h"
#include "xil_printf.h"
#include <string.h>

/* ---- external globals (живые регистровые структуры, как в cmd_udp.c) ---- */
extern struct ppos_tx        tx_chan0;
extern struct ppos_rx        rx_chan0;
extern struct peleng         pel;
extern struct ppos_par       ppos;
extern struct ad9361_rf_phy *ad9361_phy;

struct radio_params current_radio;

void radio_params_capture(struct radio_params *rp)
{
    rp->tx         = tx_chan0;
    rp->rx         = rx_chan0;
    rp->pel        = pel;
    rp->ppos_creg2 = ppos.creg2.word;
    rp->ppos_creg5 = ppos.creg5.word;
}

void radio_params_apply(const struct radio_params *rp)
{
    /* --- restore структур (run-биты гасим: безопасный старт, как в modem_init) --- */
    tx_chan0 = rp->tx;  tx_chan0.creg0.inside.run = 0;
    rx_chan0 = rp->rx;  rx_chan0.creg0.inside.run = 0;
    pel      = rp->pel;
    ppos.creg2.word = rp->ppos_creg2;
    ppos.creg5.word = rp->ppos_creg5;

    /* --- TX-канал: creg0..3 --- */
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0 * 4, (u32)tx_chan0.creg0.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 1 * 4, (u32)tx_chan0.creg1.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 2 * 4, (u32)tx_chan0.creg2.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 3 * 4, (u32)tx_chan0.creg3.word);

    /* --- RX-канал: creg0..1 --- */
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0 * 4, (u32)rx_chan0.creg0.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 1 * 4, (u32)rx_chan0.creg1.word);

    /* --- ppos: creg2 (GPIO/УФ), creg5 (slot_dur). creg3 (роль) НЕ трогаем. --- */
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR,  8, (u32)ppos.creg2.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 20, (u32)ppos.creg5.word);

    /* --- Пеленг: creg0..2 --- */
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG1_BASEADDR + PELENG_PAR_OFFSET, 0, pel.creg0.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG1_BASEADDR + PELENG_PAR_OFFSET, 4, pel.creg1.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG1_BASEADDR + PELENG_PAR_OFFSET, 8, pel.creg2.word);

    /* --- Производные настройки AD9361 (частоты в кГц*1000, аттенюация в мдБ) --- */
    ad9361_set_tx_lo_freq(ad9361_phy, (uint64_t)tx_chan0.creg2.word * 1000ULL);
    ad9361_set_rx_lo_freq(ad9361_phy, (uint64_t)rx_chan0.creg1.word * 1000ULL);
    ad9361_set_tx_attenuation(ad9361_phy, 1,
                              (uint32_t)tx_chan0.creg3.inside.ad9361_tx_atten * 250u);

    xil_printf("[CFG] radio applied: tx=%lu kHz rx=%lu kHz atten_raw=%u\r\n",
               (unsigned long)tx_chan0.creg2.word, (unsigned long)rx_chan0.creg1.word,
               (unsigned)tx_chan0.creg3.inside.ad9361_tx_atten);
}
