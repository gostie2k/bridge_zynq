#ifndef RADIO_PARAMS_H_
#define RADIO_PARAMS_H_

#include <stdint.h>
#include "modem.h"   /* struct ppos_tx / ppos_rx / peleng */

/* ===================================================================
 *  Радиопараметры КРТ — часть сохраняемого профиля (сеть см. device_config).
 *
 *  Хранит РЕГИСТРОВЫЕ ОБРАЗЫ, которыми управляет creg-панель (0xA0 группы 1/2/4
 *  и ppos creg2/5): tx_chan0, rx_chan0, peleng, ppos.creg2/creg5. Роль (ppos
 *  creg3) сюда НЕ входит — она в device_config и применяется apply_role_to_pl().
 *
 *  Модель: живые структуры (tx_chan0/rx_chan0/pel/ppos) — источник истины во
 *  время работы; при сохранении делаем их снимок (radio_params_capture), пишем
 *  в QSPI; при старте — restore + применение к железу (radio_params_apply).
 * =================================================================== */
struct radio_params {
    struct ppos_tx tx;        /* tx_chan0: creg0(mod/bod/pkt_len...), creg2(freq кГц), creg3(атт.) */
    struct ppos_rx rx;        /* rx_chan0: creg0, creg1(freq кГц) */
    struct peleng  pel;       /* пеленг creg0..2 */
    uint32_t ppos_creg2;      /* GPIO/УФ (ppos.creg2) */
    uint32_t ppos_creg5;      /* slot_dur (ppos.creg5) */
};

/* Единственный экземпляр (буфер профиля для сохранения/загрузки). */
extern struct radio_params current_radio;

/* Снять снимок ЖИВЫХ регистров (tx_chan0/rx_chan0/pel/ppos) в *rp.
 * Вызывать непосредственно перед записью профиля в QSPI. */
void radio_params_capture(struct radio_params *rp);

/* Применить *rp к железу: restore структур + запись AXI-регистров +
 * производные вызовы AD9361 (частоты TX/RX, аттенюация TX). run-биты
 * сбрасываются (безопасный старт, как в modem_init). */
void radio_params_apply(const struct radio_params *rp);

#endif /* RADIO_PARAMS_H_ */
