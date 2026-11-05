#ifndef RADIO_PARAMS_H_
#define RADIO_PARAMS_H_

#include <stdint.h>
#include "modem.h"   /* ppos_tx / ppos_rx / peleng */

/* Radio part of the saved profile, network part is in device_config.
 * Register images only. Role (ppos creg3) is not here. */
struct radio_params {
    struct ppos_tx tx;        /* creg0, creg2 freq kHz, creg3 atten */
    struct ppos_rx rx;        /* creg0, creg1 freq kHz */
    struct peleng  pel;
    uint32_t ppos_creg2;      /* GPIO */
    uint32_t ppos_creg5;      /* slot_dur */
};

extern struct radio_params current_radio;

void radio_params_capture(struct radio_params *rp);      /* snapshot live regs */
void radio_params_apply(const struct radio_params *rp);  /* restore + write AXI + AD9361 */

#endif /* RADIO_PARAMS_H_ */
