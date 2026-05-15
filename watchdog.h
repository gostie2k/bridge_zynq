#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* ===================================================================
 *  Hardware watchdog (XScuWdt) + software heartbeat supervision
 *  ------------------------------------------------------------------
 *  Назначение: при зависании любого из hot-path модулей PS получает
 *  жёсткий reset через WDRESETREQ → SLCR.
 *
 *  Архитектура:
 *    - wdt_init() конфигурирует XScuWdt с timeout WDT_TIMEOUT_MS.
 *    - wdt_start() запускает таймер. После этого ОБЯЗАТЕЛЬНО
 *      периодически вызывать wdt_kick() реже, чем WDT_TIMEOUT_MS.
 *    - Каждый hot-path модуль (main loop, lwIP RX, radio TX/RX)
 *      периодически вызывает свой wdt_hb_*() — это software heartbeat.
 *    - wdt_kick() проверяет возраст всех heartbeat'ов. Если хоть один
 *      "протух" (age > порог) — не перезаряжает таймер. Через WDT_TIMEOUT_MS
 *      PS получит soft reset.
 *
 *  Под отладчиком (JTAG) определи WDT_DISABLED глобально (например
 *  через -DWDT_DISABLED в Debug-конфиге Vitis) — все API станут no-op.
 *
 *  Для диагностики hang-теста (когда reset не происходит и нужно
 *  понять причину) определи WDT_DIAG_HANG — wdt_simulate_hang() будет
 *  печатать состояние WDT/GIC/CPSR каждые 200 мс.
 * =================================================================== */

#ifndef WDT_TIMEOUT_MS
#define WDT_TIMEOUT_MS  2000u    /* при отсутствии kick'а — soft reset */
#endif

/* Пороги "протухания" каждого heartbeat'а в миллисекундах.
 * Если модуль не отметился дольше — он считается зависшим.
 *
 * Замечание: эти пороги должны быть БОЛЬШЕ нормальной длительности
 * одной итерации main loop, иначе будут ложные ресеты. SPI к AD9361
 * и тяжёлые UDP-команды могут занимать сотни мс. */
#define WDT_HB_MAIN_MAX_MS  1000u   /* main loop — один проход обычно <100ms */
#define WDT_HB_LWIP_MAX_MS  1500u   /* xemacif_input может молчать в простое */
#define WDT_HB_TX_MAX_MS    5000u   /* радио-TX может простаивать */
#define WDT_HB_RX_MAX_MS    5000u   /* радио-RX может простаивать */

/* Какие heartbeat'ы УЧИТЫВАЮТСЯ при проверке.
 * MAIN — обязательно. Остальные включай, когда уверен что они
 * генерируются в нормальном режиме работы. */
#define WDT_CHECK_MAIN   1
#define WDT_CHECK_LWIP   0   /* выключено: в простое трафика нет */
#define WDT_CHECK_TX     0   /* выключено: радио-TX опционален */
#define WDT_CHECK_RX     0   /* выключено: радио-RX опционален */

/* ===================================================================
 *  API
 * =================================================================== */

/* Forward declaration — чтобы не тащить весь xscugic.h в шапку. */
struct XScuGic;

/* Инициализация XScuWdt в TIMER mode + регистрация ISR.
 * ISR при таймауте дёргает SLCR.PSS_RST_CTRL.SOFT_RST → полный PS reset
 * с перезапуском FSBL (НЕ только CPU reset как в watchdog-mode).
 *
 * Требует уже инициализированный XScuGic.
 * Возвращает 0 при успехе, !=0 при ошибке. */
int  wdt_init(struct XScuGic *gic);

/* Запуск таймера. После вызова обязательно периодически кикать. */
void wdt_start(void);

/* Останов таймера (для отладки). */
void wdt_stop(void);

/* Проверить heartbeat'ы и при OK перезарядить таймер.
 * Вызывать из main loop в конце каждой итерации. */
void wdt_kick(void);

/* Heartbeat'ы — вызывать из соответствующих hot-path функций.
 * Дёшёвы: один volatile store + чтение XTime. */
void wdt_hb_main(void);
void wdt_hb_lwip(void);
void wdt_hb_tx(void);
void wdt_hb_rx(void);

/* ===================================================================
 *  Диагностика
 * =================================================================== */

typedef struct {
    uint32_t reboot_status;     /* SLCR.REBOOT_STATUS, прочитанный при старте */
    uint32_t wdt_reset_flag;    /* 1 если предыдущий ресет был от WDT */
    uint32_t kick_count;        /* счётчик успешных kick'ов */
    uint32_t starve_count;      /* счётчик случаев "heartbeat протух" */
    uint32_t last_starve_mask;  /* битовая маска heartbeat'ов которые протухли в последний раз */
    uint32_t uptime_sec;        /* секунд с момента wdt_start */
} wdt_stats_t;

/* Битовая маска heartbeat'ов (для last_starve_mask) */
#define WDT_HB_BIT_MAIN  (1u << 0)
#define WDT_HB_BIT_LWIP  (1u << 1)
#define WDT_HB_BIT_TX    (1u << 2)
#define WDT_HB_BIT_RX    (1u << 3)

void wdt_get_stats(wdt_stats_t *out);

/* Сохранить и обнулить SLCR.REBOOT_STATUS. Вызвать ОДИН РАЗ как можно
 * раньше в main() до init'а — иначе следующая запись в REBOOT_STATUS
 * может затереть причину. */
void wdt_capture_reboot_status(void);

/* Симуляция зависания — бесконечный цикл с отключённым kick'ом.
 * Используется для тестирования через UDP 0x90. Не возвращается. */
void wdt_simulate_hang(void) __attribute__((noreturn));

/* Прямой триггер PS soft reset через SLCR, минуя WDT/ISR.
 * Используется для диагностики: если этот вызов ресетит PS, а
 * wdt_simulate_hang не ресетит — значит проблема в ISR/GIC.
 * Не возвращается. */
void wdt_force_reset_now(void) __attribute__((noreturn));

#endif /* WATCHDOG_H */
