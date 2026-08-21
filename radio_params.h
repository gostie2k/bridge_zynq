#ifndef RADIO_PARAMS_H_
#define RADIO_PARAMS_H_

#include <stdint.h>
#include "modem.h"   /* struct ppos_tx / ppos_rx / peleng */

/* ===================================================================
 * Radio parameters - part of the saved profile (network: see device_config).
 *
 * Holds REGISTER IMAGES driven by the creg-panel (0xA0 groups 1/2/4
 * and ppos creg2/5): tx_chan0, rx_chan0, peleng, ppos.creg2/creg5. Role (ppos
 * creg3) is NOT here - it is in device_config and applied by apply_role_to_pl().
 *
 * Model: live structs (tx_chan0/rx_chan0/pel/ppos) are the source of truth at
 * runtime; on save we snapshot them (radio_params_capture), write
 * to QSPI; at startup - restore + apply to hardware (radio_params_apply).
 * =================================================================== */
struct radio_params {
    struct ppos_tx tx;        /* tx_chan0: creg0(mod/bod/pkt_len...), creg2(freq kHz), creg3(atten) */
    struct ppos_rx rx;        /* rx_chan0: creg0, creg1(freq kHz) */
    struct peleng  pel;       /* peleng creg0..2 */
    uint32_t ppos_creg2;      /* GPIO/UF (ppos.creg2) */
    uint32_t ppos_creg5;      /* slot_dur (ppos.creg5) */
};

/* The single instance (profile buffer for save/load). */
extern struct radio_params current_radio;

/* Snapshot the LIVE registers (tx_chan0/rx_chan0/pel/ppos) into *rp.
 * Call right before writing the profile to QSPI. */
void radio_params_capture(struct radio_params *rp);

/* Apply *rp to hardware: restore structs + write AXI registers +
 * derived AD9361 calls (TX/RX freq, TX attenuation). run bits
 * are cleared (safe start, as in modem_init). */
void radio_params_apply(const struct radio_params *rp);

#endif /* RADIO_PARAMS_H_ */
