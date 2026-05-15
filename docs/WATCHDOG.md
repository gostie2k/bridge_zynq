# Watchdog

Hardware watchdog + software heartbeat supervision для PS-прошивки модема. Цель — гарантированный reset PS при зависании любого hot-path модуля (main loop, lwIP, радио-TX/RX), чтобы модем не "залипал" в полевых условиях.

Файлы: `watchdog.h`, `watchdog.c`. Интеграция: `main.c` (init/kick/heartbeat), `cmd_udp.c` (диагностические команды).

---

## Архитектура

```
                              ┌─────────────────────────────┐
                              │   main loop / lwIP / TX/RX  │
                              │   периодически дёргают:     │
                              │     wdt_hb_main()           │
                              │     wdt_hb_lwip()           │
                              │     wdt_hb_tx() / _rx()     │
                              └──────────────┬──────────────┘
                                             │ timestamps
                                             ▼
                              ┌─────────────────────────────┐
                              │   wdt_kick() в конце main   │
                              │   loop проверяет возраст    │
                              │   всех heartbeat'ов:        │
                              │     OK  → XScuWdt_RestartWdt│
                              │     STARVE → не перезаряжаем│
                              └──────────────┬──────────────┘
                                             │
                       не было restart        │
                       2 секунды              ▼
                              ┌─────────────────────────────┐
                              │   SCU WDT counter → 0       │
                              │   IT_ENABLE → ISR (IRQ 30)  │
                              └──────────────┬──────────────┘
                                             ▼
                              ┌─────────────────────────────┐
                              │   wdt_isr → wdt_trigger_    │
                              │   soft_reset():             │
                              │     1. UART flush           │
                              │     2. SLCR unlock          │
                              │     3. REBOOT_STATUS magic  │
                              │     4. SLCR.PSS_RST_CTRL.   │
                              │        SOFT_RST = 1         │
                              └──────────────┬──────────────┘
                                             ▼
                              ┌─────────────────────────────┐
                              │   PS soft reset             │
                              │   BootROM → FSBL → app      │
                              │   (требует QSPI/SD boot     │
                              │    mode; при JTAG app не    │
                              │    перезагрузится)          │
                              └─────────────────────────────┘
```

Hardware-таймер — **SCU Private Watchdog** (XScuWdt, base `0xF8F00620`, IRQ 30 PPI). Работает в **TIMER mode**, при таймауте только генерирует прерывание (а не сам триггерит CPU reset как в watchdog-mode). Поэтому ISR явно записывает `SLCR.PSS_RST_CTRL.SOFT_RST` чтобы получить **full PS reset** (включая FSBL re-run, DDR/PLL re-init), а не reset одного CPU.

Software heartbeats — это `volatile XTime` per-module: каждый hot-path-модуль пишет туда текущий timestamp. `wdt_kick()` смотрит на возраст каждого и решает можно ли перезаряжать hardware-таймер. Если хоть один heartbeat "протух" (старше своего порога) — таймер не перезаряжается и через `WDT_TIMEOUT_MS` срабатывает.

---

## Конфигурация

Все настройки — `#define` в `watchdog.h`. Меняй там же.

| Define | Значение | Что значит |
|---|---|---|
| `WDT_TIMEOUT_MS` | 2000 | Hardware timeout. Если `wdt_kick()` не вызывался дольше — reset. |
| `WDT_HB_MAIN_MAX_MS` | 1000 | Сколько мс main loop может "молчать". Один проход обычно <100 мс. |
| `WDT_HB_LWIP_MAX_MS` | 1500 | Для `xemacif_input` heartbeat'а. Не учитывается по умолчанию. |
| `WDT_HB_TX_MAX_MS` | 5000 | Радио-TX может простаивать. Не учитывается по умолчанию. |
| `WDT_HB_RX_MAX_MS` | 5000 | Радио-RX может простаивать. Не учитывается по умолчанию. |
| `WDT_CHECK_MAIN` | 1 | Учитывать ли MAIN heartbeat при kick'е. |
| `WDT_CHECK_LWIP` | 0 | Отключено — в простое трафика нет, heartbeat не дёргается. |
| `WDT_CHECK_TX` | 0 | Отключено — радио-TX опционален. |
| `WDT_CHECK_RX` | 0 | Отключено — радио-RX опционален. |

Прескалер SCU WDT — 255 (делитель 256). При PERIPHCLK 333 MHz даёт ~1.3 M тик/сек, что при `WDT_TIMEOUT_MS=2000` укладывается в 32-битный счётчик (load=`0x0027BC86`).

### Build-time флаги

| Флаг | Эффект |
|---|---|
| `-DWDT_DISABLED` | Все API становятся no-op. Используй под JTAG-отладкой чтобы WDT не мешал. |
| `-DWDT_DIAG_HANG` | `wdt_simulate_hang()` печатает state WDT/GIC/CPSR каждые 200 мс. Для диагностики когда reset не происходит. |

---

## API

```c
void wdt_capture_reboot_status(void);   /* ВЫЗВАТЬ ПЕРВЫМ в main() */
int  wdt_init(struct XScuGic *gic);     /* После init_ScuGic */
void wdt_start(void);                   /* После всех init'ов */
void wdt_stop(void);                    /* Для отладки */
void wdt_kick(void);                    /* В конце каждой итерации main loop */

void wdt_hb_main(void);                 /* Из main loop */
void wdt_hb_lwip(void);                 /* После xemacif_input */
void wdt_hb_tx(void);                   /* Из радио-TX */
void wdt_hb_rx(void);                   /* Из радио-RX */

void wdt_get_stats(wdt_stats_t *out);   /* Для диагностики через UDP */
void wdt_simulate_hang(void);           /* Тест — уходит в бесконечный цикл */
void wdt_force_reset_now(void);         /* Тест — прямой SLCR reset */
```

`wdt_capture_reboot_status()` — **обязательно первый вызов в `main()`**, до любого init'а, который может затереть `SLCR.REBOOT_STATUS`. Читает причину последнего reset'а, выставляет `g_stats_wdt_reset_flag` если ресет был от WDT, и затем чистит биты-причины чтобы следующий ресет имел корректную причину.

---

## Boot mode requirements

WDT-reset работает корректно только если PS может **сам перезагрузиться** после soft reset'а. Это требует наличия FSBL в загрузочном носителе:

| Boot mode | После WDT reset |
|---|---|
| **QSPI / SD card** | BootROM → FSBL из flash → ваше приложение. WDT перезагружает модем целиком. **Production-режим.** |
| **JTAG** (Vitis Run/Debug, без debug session) | PS ресетится, UART замолкает. BootROM ждёт JTAG-загрузки нового app'а. **WDT работает, но приложение не перезапустится автоматически.** |

Для верификации WDT в полевых условиях нужен boot.bin в QSPI/SD. Тестирование под JTAG показывает только что reset происходит (UART замолкает) — но не что прошивка восстановилась.

---

## UDP-команды (через `0x90`)

Все команды отправляются на UDP-порт 60012 в формате `[0x90][sub][payload]`. Подробнее см. `CONTROL_PLANE.md`.

### `0x90 0x40` — Запрос wdt stats

Без payload. Ответ (24 байта payload после `[0x90][0x40]`):

| Offset | Поле | Назначение |
|---|---|---|
| 2..5 | `reboot_status` (LE32) | Захваченный `SLCR.REBOOT_STATUS` при старте |
| 6 | `wdt_reset_flag` | 1 если предыдущий reset был от WDT |
| 7 | reserved | 0 |
| 8..11 | `kick_count` (LE32) | Сколько раз `wdt_kick` успешно перезарядил таймер |
| 12..15 | `starve_count` (LE32) | Сколько раз heartbeat'ы "протухли" |
| 16..19 | `last_starve_mask` (LE32) | Битовая маска кто протух последним (см. `WDT_HB_BIT_*`) |
| 20..23 | `uptime_sec` (LE32) | Секунд с момента `wdt_start()` |

**Декодирование `reboot_status`** (биты 22..16):

| Бит | Имя | Причина |
|---|---|---|
| 22 | `POR_INT` | Power-on reset |
| 21 | `SWDT_RST_INT` | System WDT |
| 20 | `AWDT0_RST_INT` | CPU0 SCU WDT |
| 19 | `AWDT1_RST_INT` | CPU1 SCU WDT |
| 18 | `SRST_B_INT` | Внешний reset pin |
| 17 | `DBG_RST_INT` | Debugger |
| 16 | `SLC_RST_INT` | `SLCR.PSS_RST_CTRL.SOFT_RST` (это и есть наш ISR-инициированный путь) |
| 4 | `WDT_MAGIC` | Наш magic-флаг (ставится ISR'ом перед SLCR write) |

Если `wdt_reset_flag=1` и в `reboot_status` стоят `SLC_RST_INT|WDT_MAGIC` — это reset, инициированный нашим watchdog'ом.

### `0x90 0x41 0x5A` — Симуляция зависания

Magic byte `0x5A` обязателен (защита от случайного вызова). Прошивка отвечает `[0x90][0x41][0x01]` и сразу уходит в бесконечный цикл — `wdt_kick` перестаёт вызываться, через `WDT_TIMEOUT_MS` срабатывает ISR и PS ресетится.

Если собрано с `-DWDT_DIAG_HANG`, перед reset'ом печатается verbose-диагностика (см. ниже).

### `0x90 0x42 0xA5` — Прямой SLCR reset

Magic byte `0xA5` обязателен. Прошивка отвечает `[0x90][0x42][0x01]` и **немедленно** дёргает `wdt_trigger_soft_reset()`, минуя WDT и ISR. Используется для диагностики чтобы отделить проблемы в WDT/ISR-пути от проблем в самом SLCR-reset механизме.

---

## Recovery — локальный «soft restart» без PS reset

PS reset через WDT — тяжёлая операция (~3 сек простоя, заново AD9361 init ~1.1 сек, DDR/PLL re-init). Для случаев когда залип конкретный модуль, есть «локальный recovery» — чистка state одного модуля без перезагрузки всего PS.

### Реализованные recovery-handler'ы

| Функция | Что чинит | Что НЕ трогает |
|---|---|---|
| `radio_recover_tx()` | AXI FIFO TX side + ring `tx_ring` + `global_tx_pkt/seq_counter` | RX, stats, AD9361 |
| `radio_recover_rx()` | AXI FIFO RX side + parser state `rx_state` + `rx_pkt_*`/`rx_calc_crc` | TX, stats, AD9361 |
| `bridge_recover_lwip()` | dynamic FDB entries у `bridge_netif` (учим MAC заново) | static FDB, UDP PCB, netif state |

Stats-счётчики ошибок (err_seq_gap, err_crc, ...) НЕ обнуляются — нужны для оценки частоты срабатываний recovery.

### UDP-команды (ручной trigger)

Все требуют magic byte `0x52` (= ASCII 'R') в `payload[2]`. Защита от случайного дёргания.

#### `0x90 0x43 0x52` — Recover lwIP

Чистит dynamic FDB-таблицу bridge_netif. После cleanup bridgeif начинает заново выучивать активные MAC из трафика. Static-entries (16 захардкоженных MAC'ов модемов из `BRIDGEIF_INITDATA1`) не трогаются.

Когда полезно:
- bridge выучил MAC на одном порту, потом устройство мигрировало → пакеты уходят не туда
- таблица заполнена устаревшими entries (timer aging отключён в нашем форке `bridgeif_fdb.c`)
- lwIP stats показывают drops, но UDP control plane работает

Ответ: `[0x90][0x43][0x01][cleared_lo][cleared_hi]` — количество очищенных entries (LE16).

#### `0x90 0x44 0x52` — Recover radio TX

- `XLlFifo_TxReset(&fifo)` — drop всё что не успело уйти в PL
- `tx_head/tail/count = 0`
- `global_tx_pkt_counter/seq_counter = 0`

Когда полезно:
- `err_fifo_full` растёт лавинообразно (PS не успевает выгребать в FIFO, FIFO забит)
- TX heartbeat «протух» — kernel зависал, FIFO в неконсистентном состоянии
- После долгой паузы хочется чистый старт нумерации сегментов

⚠️ Удалённый конец (master/slave) увидит **разрыв в `seq` и `pkt_id`** — `err_seq_gap`/`err_pkt_gap` у него инкрементнутся. Это нормально для recovery.

Ответ: `[0x90][0x44][0x01]`.

#### `0x90 0x45 0x52` — Recover radio RX

- `XLlFifo_RxReset(&fifo)` — drop всё что не успели разобрать
- Сброс RX парсера: `rx_state = ST_MAGIC_H`, `rx_pkt_len/received = 0`, `rx_calc_crc/recv_crc = 0`
- `rx_first_block_seen/pkt_seen = 0` — следующий полученный block становится новой точкой отсчёта

Когда полезно:
- `err_crc` растёт но AD9361 заведомо OK (CRC-кейс может означать desync парсера)
- RX heartbeat «протух» — `drain_rx_fifo` подавился неконсистентным block'ом
- После manual TX recovery полезно сделать и RX recovery (parser ждал продолжения старого пакета)

Ответ: `[0x90][0x45][0x01]`.

### Безопасность recovery

| Что | Безопасно? |
|---|---|
| Вызов из `cmd_udp_callback` (UDP-handler) | ✅ да |
| Вызов из main loop | ✅ да |
| Вызов из ISR | ❌ нет — `xil_printf` и FIFO writes не reentrant |
| Параллельный вызов TX и RX recovery | ✅ да — они трогают разные данные |
| Параллельный вызов с активным trafic'ом | ⚠️ возможна потеря в момент reset'а (~10 us) — нормально |

### Roadmap

**Этап 2 (не реализовано):** автоматический trigger из `wdt_kick` по протуханию соответствующего heartbeat'а. Идея: HB starve → попробовать local recovery → если через N попыток не помогло → escalate до PS reset.

Сейчас все recovery — ручные, через UDP-команды. После того как они валидированы в полевых условиях (не ломают связь, не теряют трафик), можно подключать к watchdog'у автоматику.

---

## Диагностика

### Команда не работает / reset не происходит

1. Проверь `0x90 0x40` — `kick_count` должен расти. Если `kick_count=0` или не растёт — WDT не запустился или `wdt_kick()` не вызывается в main loop.
2. Собери с `-DWDT_DIAG_HANG`, отправь `0x90 0x41 0x5A`. В логе будет:
   - `CPSR` (I-bit должен быть 0 чтобы IRQ ходили)
   - `CTRL` WDT (WD_ENABLE=1, IT_ENABLE=1, WD_MODE=0, prescaler=255)
   - `LOAD`/`COUNT`/`ISR_FLAG` SCU WDT
   - `GIC ISENABLER0` (бит 30 должен быть 1)
   - `PMR` (0xF0 — все приоритеты пропускаются)
   - Каждые 200 мс `COUNT` должен убывать, дойти до ~0 за 2 секунды.
3. Если counter убывает но ISR не вызывается — проблема в GIC (бит 30 не выставлен или PMR слишком строгий).
4. Если ISR вызывается (видишь начало `[WDT] TIMEOUT...` в логе) но PS не ресетится — проблема в SLCR. Попробуй `0x90 0x42 0xA5` — этот путь минует ISR. Если и он не ресетит — проверь boot mode (см. выше).

### Под JTAG лог обрывается, прошивка не возвращается

Это нормально. PS reset произошёл, но BootROM ждёт JTAG-загрузки. Под JTAG невозможно автоматически перезапустить приложение — нужно либо подложить boot.bin в flash, либо запускать через `xsct` с auto-reload скриптом.

### UART лог обрывается на `[WDT` без полного `TIMEOUT`

`xil_printf` в ISR не успевает дотолкать строку в UART TX FIFO до того как SLCR reset убивает peripherals. Текущий `wdt_trigger_soft_reset` делает busy-wait на TX FIFO empty с таймаутом ~10 мс — в обычной ситуации хватает. Если всё равно режет — увеличь `spin` в `wdt_uart_flush()` или укоротите ISR-лог.

---

## Известные ограничения

- **WDT не сработает если main loop крутится но не вызывает `wdt_hb_main()`.** Heartbeat-проверка в `wdt_kick` основана на возрасте timestamp'а. Если main loop вошёл в "тихий зависон" — продолжает работать, но не отмечает heartbeat — WDT сработает. Если "молчаливо крутит kick'и без hb_main" — нет (но такого кода в проекте нет).
- **Если все IRQ замаскированы** (`Xil_ExceptionDisable` где-нибудь не сбалансирован) — ISR не выстрелит, и WDT в timer mode не сможет инициировать reset. Watchdog-mode (`WD_MODE=1`) сбросил бы CPU без ISR, но только текущий CPU, не PS — поэтому мы выбрали timer mode + ISR.
- **LWIP/TX/RX heartbeat'ы по умолчанию отключены** (`WDT_CHECK_*=0`). Включай по одному, проверяя что в нормальном режиме они действительно дёргаются чаще порога — иначе будут ложные reset'ы.
- **`xil_printf` в hot-path может задержать heartbeat'ы.** Длинные UDP-команды (например AD9361 SPI) занимают сотни мс. Поэтому `WDT_HB_MAIN_MAX_MS=1000` — с запасом.

---

## История изменений

- **Initial** (2026-05-15): Hardware WDT через XScuWdt в timer mode, software heartbeats (MAIN активен), reset через `SLCR.PSS_RST_CTRL.SOFT_RST` из ISR. UDP-команды `0x40`/`0x41`/`0x42`. UART flush перед reset'ом. Verbose-диагностика hang'а через `-DWDT_DIAG_HANG`.
