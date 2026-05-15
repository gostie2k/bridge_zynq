# Control Plane

Описание управляющего тракта модема: переход от protocol `0x90` к управлению через AXI cregs (`0xA0`), карта регистров, что осталось от `0x90` и почему.

---

## История перехода

**Раньше (только `0x90`):** все настройки модема — частоты, аттенюация, GPIO, FPGA-регистры — выполнялись командами протокола `0x90` поверх UDP. Каждая команда приводила к одной из:
- записи в SPI AD9361 (для частот, аттенюации, fastlock и т. п.)
- записи 11-битной SPI-команды в FPGA-регистр (par-регистры PPOS DSP)
- записи в SPI ячейки COM/POW-board

**Теперь (`0xA0` + остаточный `0x90`):** инициализация и runtime-управление основными параметрами модема выполняется записью в **AXI control-регистры (cregs)**. Это структурированные регистры, описанные C-объединениями (битовые поля + 32-битное слово). Запись идёт через `AXI_CNTRL_REG_WRITE` напрямую в PL.

Команды `0x90` сохранены **только** для:
- совместимости со старыми Qt-программами;
- функций, не покрытых cregs (NPU команды, fastlock store/recall, GPIO mask, диагностика).

При записи cregs, относящихся к параметрам AD9361 (частоты, аттенюация), параллельно вызываются функции no-OS драйвера AD9361 — см. примечание в конце документа.

---

## Карта AXI

Базовый адрес: `AXI_CNTRL_REG_BASEADDR = XPAR_PPOS_DSP_0_S00_AXI_BASEADDR`.

Три блока регистров:

| Offset (от base) | Define              | Блок              | Размер  | Кол-во cregs |
|------------------|---------------------|-------------------|---------|--------------|
| `0x00`           | `BASE_PAR_OFFSET`   | `ppos.cregN`      | 6 cregs | 0..5         |
| `0x20`           | `TX_CHAN0_PAR_OFFSET` | `tx_chan0.cregN`| 4 cregs | 0..3         |
| `0x30`           | `RX_CHAN0_PAR_OFFSET` | `rx_chan0.cregN`| 2 cregs | 0..1         |

Адрес конкретного creg: `base + block_offset + N*4`. Каждый creg — 32-битное слово.

**Доступ из C:**
```c
#include "modem.h"

// Запись через DRIVER macro (resolves to Xil_Out32):
AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0,
                    tx_chan0.creg0.word);

// Чтение:
u32 word = AXI_CNTRL_REG_READ(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0);
```

---

## Глобальные регистры — `ppos.cregN`

Структура: `struct ppos_par` в `modem.h`. Базовый offset `0x00`.

### `ppos.creg0` — version (read-only из software)

| Биты   | Поле   | Назначение                        |
|--------|--------|-----------------------------------|
| 0..11  | `sn`   | Серийный номер прошивки PL        |
| 12..19 | `pv`   | Product version                   |
| 20..31 | `pn`   | Product number                    |

Используется для идентификации сборки PL. В коде сейчас не читается.

### `ppos.creg1` — control pulses

Импульсные сигналы (записать в `1`, PL обработает по фронту):

| Биты | Поле                      | Назначение                                        |
|------|---------------------------|---------------------------------------------------|
| 0    | `imit_ps_int_gpio_63`     | Имитация прерывания GPIO 63 (отладка)             |
| 1    | `imit_ps_int_gpio_62`     | Имитация прерывания GPIO 62 (отладка)             |
| 2    | `mfunc_start`             | Запуск multi-function блока                       |
| 3    | `test_gen_set_fc`         | Установить частоту тестового генератора (по `creg1` TX) |
| 4    | `set_dac_ltc2630`         | Применить значение DAC LTC2630 (см. `creg5`)      |
| 5..31| reserve                   |                                                   |

В коде сейчас не используется. Зарезервировано под будущую обвязку.

### `ppos.creg2` — hardware settings ⚠️

Самый «горячий» регистр в PL → PS обмене. **Часть бит — write (PS → PL), часть — read (PL → PS).** Direction отдельных битов **требует уточнения у разработчика PL** (см. примечание в конце).

| Биты   | Поле          | Direction | Назначение                                    |
|--------|---------------|-----------|-----------------------------------------------|
| 0      | `Rx1B`        | W         | Выбор RX-входа AD9361: 0=A, 1=B               |
| 1      | `Tx1B`        | W         | Выбор TX-выхода AD9361: 0=A, 1=B              |
| 2      | `Rx2B`        | W         | Аналогично для второго канала                 |
| 3      | `Tx2B`        | W         | Аналогично для второго канала                 |
| 4..9   | `GPIO1R..6R`  | W         | Состояния пользовательских GPIO ячеек         |
| 10..15 | `GPIO1R_DIR..6R_DIR` | W  | Направления тех же GPIO (0=in, 1=out)         |
| 16..17 | `GPIOnR_TYPE` | W         | Тип логики GPIO                               |
| 18     | `Tang`        | W         | Управление тангентой (PRM/PRD)                |
| 19     | `Tang_avt`    | W         | Авто-режим тангенты                           |
| 20     | `Pit`         | W         | Питание                                       |
| 21..23 | `Fdiap`       | W         | Выбор частотного диапазона: 0=3.4-3.6, !0=3.7-3.9 |
| 24..29 | `GPIO1R_IN..6R_IN` | R    | Чтение реальных уровней GPIO                  |
| 30..31 | reserve       |           |                                               |

**Известно:**
- V_TEMP, V_POWER читаются — но эти биты в текущей структуре `creg2` не выделены явно (возможно, они в другом creg или часть `GPIOnR_IN`).
- Tang, Fdiap — пишутся PS, читаются PL.
- Записывается в `modem_init()` и через `ppos_set_tangenta()`, `spi_engine_npu_pow_board_set_f_diap()` (последняя пишет и в creg, и в SPI POW-board).

### `ppos.creg3` — gain board / slave-modem линия

| Биты | Поле                     | Назначение                          |
|------|--------------------------|-------------------------------------|
| 0    | `nuf_pit`                | Питание UF                          |
| 1    | `uf_tangenta`            | Тангента UF                         |
| 2    | `uf_sel`                 | Селектор UF                         |
| 3    | `uf_tangenta_ena_avt`    | Авто-режим тангенты UF              |
| 4..31| reserve                  |                                     |

В текущем коде в `modem_init()` не инициализируется, может писаться через 0x90 (sub `0x08`).

### `ppos.creg4` — TXNRX timing

| Биты   | Поле          | Default | Назначение                                |
|--------|---------------|---------|-------------------------------------------|
| 0..15  | `txnrx_setup` | 2000    | TXNRX setup time (тактов или нс — у PL)   |
| 16..31 | `txnrx_hold`  | 4500    | TXNRX hold time                           |

Записывается в `modem_init()` один раз.

### `ppos.creg5` — LTC2630 DAC

| Биты  | Поле                | Назначение                         |
|-------|---------------------|------------------------------------|
| 0..7  | `ltc2630_dac_data`  | Данные для LTC2630                 |
| 8..11 | `ltc2630_dac_com`   | Команда LTC2630                    |
| 12..31| reserve             |                                    |

Применяется по импульсу `creg1.set_dac_ltc2630`.

### Дополнительные поля `struct ppos_par` (в RAM, не AXI)

```c
uint8_t   status_on;
uint8_t   status_ip[4];     // {192,168,250,22} по умолчанию
uint16_t  status_port;       // 5007 по умолчанию
struct ppos_tx* ppos_txs;
struct ppos_rx* ppos_rxs;
uint8_t   num_ppos_txs;
uint8_t   num_ppos_rxs;
```

Это RAM-копии настроек для отправки статуса по UDP, а не AXI-регистры.

---

## TX-канал — `tx_chan0.cregN`

Структура: `struct ppos_tx` в `modem.h`. Базовый offset `0x20`.

### `tx_chan0.creg0` — TX mode and run

| Биты   | Поле              | Default | Назначение                                          |
|--------|-------------------|---------|-----------------------------------------------------|
| 0..1   | `mod`             | 2       | Modulation type                                     |
| 2..3   | `bod`             | 2       | Bit-order or baud-related                           |
| 4..6   | `code`            | 0       | Кодирование                                         |
| 7      | `scr_on`          | 1       | Скремблер вкл/выкл                                  |
| 8..11  | `mux_ad9361_din`  | 5       | Мультиплексор данных в AD9361 DIN                   |
| 12     | `run`             | 0/1     | Запуск канала. Pseudo-handshake (см. ниже).         |
| 13..15 | reserve           |         |                                                     |
| 16..31 | `pkt_len`         | 576     | **Селектор data path режима** (576=radio_stream, 4800=legacy) |

**`run` semantics:** записывается software'ом в `1` после конфигурации creg0. PL может сбрасывать `run` обратно в `0` для индикации, что канал не активен (например, после ошибки или в режиме конфигурации). При следующей попытке отправки `send_cmd()` / `send_block_to_pl()` читают creg0 и при `run==0` сбрасывают FIFO и снова выставляют `run=1`. Это паттерн lazy-restart, не классический handshake.

**`pkt_len` semantics:** не размер пакета в строгом смысле, а селектор data path:
- `576` — продакшн-режим radio_stream (нарезка на 188-байт блоки + CRC32)
- `4800` — legacy-режим (custom-преамбула + 450-словные пакеты, для отладки PL)

См. `vport3_linkoutput()` в `bridge_func.c` и main loop в `main.c`.

### `tx_chan0.creg1` — Test generator frequency

| Биты   | Поле           | Default | Назначение                                |
|--------|----------------|---------|-------------------------------------------|
| 0..31  | `test_gen_fc`  | 0       | Частота тестового генератора (фазовый шаг)|

Применяется по импульсу `ppos.creg1.test_gen_set_fc`. В legacy `0x90` есть отдельная команда `0x05`, которая пишет ту же информацию через SPI байты `0x18`-`0x1B` PPOS DSP.

### `tx_chan0.creg2` — TX LO frequency (AD9361)

| Биты   | Поле             | Default | Назначение                              |
|--------|------------------|---------|-----------------------------------------|
| 0..31  | `ad9361_tx_freq` | 3500000 | Частота TX LO в **kHz** (не Hz!)        |

**При записи через `0xA0` (group=1, creg=2) выполняется параллельно:**
1. `AXI_CNTRL_REG_WRITE(...)` — записывает значение в PL (для статистики/синхронизации).
2. `ad9361_set_tx_lo_freq(ad9361_phy, freq * 1000)` — реально настраивает AD9361 через no-OS драйвер по SPI.

См. `handle_creg_exch()` в `cmd_udp.c`. Реальная RF-настройка идёт через no-OS драйвер; что именно делает PL с этим значением — **требует уточнения**.

### `tx_chan0.creg3` — TX attenuation

| Биты   | Поле                | Default     | Назначение                                |
|--------|---------------------|-------------|-------------------------------------------|
| 0..9   | `ad9361_tx_atten`   | 40000/250=160 | Аттенюация в единицах 250 mdB (0.25 dB)   |
| 10..15 | reserve_0           |             |                                           |
| 16..31 | reserve_1           |             |                                           |

При записи через `0xA0` (group=1, creg=3): writeback в PL + `ad9361_set_tx_attenuation(ad9361_phy, 1, atten*250)`.

Диапазон: `0..1023` единиц (0..255.75 dB), но AD9361 ограничивает реально достижимое.

---

## RX-канал — `rx_chan0.cregN`

Структура: `struct ppos_rx` в `modem.h`. Базовый offset `0x30`.

### `rx_chan0.creg0` — RX mode and run

| Биты   | Поле              | Default | Назначение                                          |
|--------|-------------------|---------|-----------------------------------------------------|
| 0..1   | `mod`             | 2       | Modulation                                          |
| 2..3   | `bod`             | 2       | Bit-order/baud                                      |
| 4..6   | `code`            | 0       | Кодирование                                         |
| 7      | `scr_on`          | 1       | Дескремблер                                         |
| 8      | `rot_on`          | 1       | Rotation correction                                 |
| 9      | `rcos_filt_on`    | 1       | Raised cosine фильтр                                |
| 10     | `freq_shift_on`   | 0       | Сдвиг частоты                                       |
| 11     | `run`             | 0/1     | Запуск канала                                       |
| 12..15 | reserve           |         |                                                     |
| 16..31 | `pkt_len`         | 576     | Селектор data path RX (симметрично TX)              |

В main loop `rx_chan0.creg0.inside.pkt_len == 4800` запускает legacy `fifo_rx_buf_write`, `== 576` — `drain_rx_fifo` (radio_stream).

### `rx_chan0.creg1` — RX LO frequency (AD9361)

| Биты   | Поле             | Default | Назначение                              |
|--------|------------------|---------|-----------------------------------------|
| 0..31  | `ad9361_rx_freq` | 3500000 | Частота RX LO в **kHz**                 |

При записи через `0xA0` (group=2, creg=1): writeback в PL + `ad9361_set_rx_lo_freq(ad9361_phy, freq*1000)`.

---

## Протокол `0xA0` — запись cregs по UDP

Порт UDP **60012**. Регистрируется через `udp_recv(cmd_udp_pcb, cmd_udp_callback, NULL)`.

### Формат пакета

| Байт | Поле     | Значение                                       |
|------|----------|------------------------------------------------|
| 0    | magic    | `0xA0`                                         |
| 1    | group    | `1` = tx_chan0, `2` = rx_chan0                 |
| 2    | creg     | Индекс creg в блоке (TX: 0..3, RX: 0..1)       |
| 3    | type     | `0x00` = set creg (только этот type реализован)|
| 4..7 | word     | 32-битное значение, big-endian (data[7]=LSB)   |

Длина пакета **8 байт** (set) или **4 байта** (зарезервировано под get, не реализовано).

### Обработка

```c
// cmd_udp.c, в cmd_udp_callback:
if ((len == 8 || len == 4) && data[0] == 0xA0)
    handle_creg_exch(upcb, data, len, addr, port);
```

`handle_creg_exch` (в `cmd_udp.c`) парсит group/creg, формирует 32-битное word из big-endian байт `data[4..7]`, и:

1. Записывает в соответствующее поле RAM-зеркала структуры (`tx_chan0.cregN.word = word`).
2. Вызывает `AXI_CNTRL_REG_WRITE(...)` для синхронизации с PL.
3. Для AD9361-параметров (TX freq, TX atten, RX freq) дополнительно вызывает no-OS API:
   - `creg2` TX → `ad9361_set_tx_lo_freq(ad9361_phy, freq*1000)`
   - `creg3` TX → `ad9361_set_tx_attenuation(ad9361_phy, 1, atten*250)`
   - `creg1` RX → `ad9361_set_rx_lo_freq(ad9361_phy, freq*1000)`

**Особенность**: при записи `creg0` TX (group=1, creg=0) **`run` принудительно сбрасывается в 0** перед записью в PL:
```c
tx_chan0.creg0.word = word;
tx_chan0.creg0.inside.run = 0;
AXI_CNTRL_REG_WRITE(...);
```
Аналогично для `creg0` RX. Это сделано чтобы при смене режима PL гарантированно остановился; перевод обратно в `run=1` происходит ленИво при следующей попытке передачи.

### Примеры

**Установить TX-частоту 3500 МГц (Master):**
```
A0 01 02 00 00 35 6F 00
└┘ └┘ └┘ └┘ └─────────┘
 │  │  │  │       └─ 0x00356F00 = 3 500 000 (kHz, big-endian: 00 35 6F 00)
 │  │  │  └────────  type=0 (set)
 │  │  └───────────  creg=2 (TX freq)
 │  └──────────────  group=1 (tx_chan0)
 └─────────────────  protocol=0xA0
```

**Установить RX-частоту 3700 МГц:**
```
A0 02 01 00 00 38 70 80
                │      │
                │      └─ 0x00387080 = 3 700 000
                └─ creg=1 (RX freq)
```

**Установить TX-аттенюацию 30 dB (= 30000 mdB / 250 = 120 единиц):**
```
A0 01 03 00 00 00 00 78
                      │
                      └─ 0x78 = 120
```

### Ответ

В текущей реализации `0xA0` ответ не отправляется — это «огонь и забудь». Для проверки — следующая команда `0x90` чтения (например `0x10` для TX freq) или диагностика `0x90 0x20`.

---

## Что осталось от `0x90`

Команды `0x90` обрабатываются в `handle_proto_0x90` (`cmd_udp.c`). Формат: `0x90 sub_cmd [payload]`.

**Активно используется** для функций, не покрытых cregs:

### Write commands

| Sub  | Длина | Что делает                                       | Status                        |
|------|-------|--------------------------------------------------|-------------------------------|
| 0x02 | 10    | Set TX LO frequency (8-байт u64 Hz, LE)          | дублирует `creg2` TX (но в Hz, не kHz!) |
| 0x03 | 10    | Set RX LO frequency (8-байт u64 Hz, LE)          | дублирует `creg1` RX          |
| 0x04 | 6     | Set TX attenuation (4-байт u32 dB, LE; ≤890)     | дублирует `creg3` TX          |
| 0x05 | 7     | Test generator (4-байт freq code + mask)         | через SPI PPOS DSP par[0x18..0x1B] |
| 0x06 | 3     | RX modulation par[0x17]                          | SPI                           |
| 0x07 | 3     | TX modulation par[0x16]                          | SPI                           |
| 0x08 | 3     | GPIO/UF mask (qt_mask → par[0x28])               | SPI; читает обратно для verify|
| 0x14 | 3     | AD9361 fastlock store (profile + txnrx)          | no-OS API                     |
| 0x15 | 3     | FHSS toggle (4 SPI команды)                      | SPI                           |
| 0x16 | 4     | NPU panel enable (через `func_type=18`)          | execute_func в main loop      |
| 0x17 | 3     | NPU preselector (через `func_type=19`)           | execute_func                  |
| 0x18 | 3     | NPU тангента (через `func_type=20`)              | execute_func                  |

### Read commands

| Sub  | Что возвращает                                        | Длина ответа |
|------|-------------------------------------------------------|--------------|
| 0x10 | TX LO frequency (u64 LE)                              | 10           |
| 0x11 | RX LO frequency (u64 LE)                              | 10           |
| 0x12 | TX attenuation (u32 dB LE)                            | 6            |
| 0x13 | par[N] (8-bit регистр PPOS DSP по SPI)                | 4            |

### Diagnostics

| Sub  | Что делает                                            | Длина ответа |
|------|-------------------------------------------------------|--------------|
| 0x20 | Stats: radio_stats_t + lwIP stats                     | 78           |
| 0x21 | Loopback get/set (radio_stream loopback)              | 3            |
| 0x22 | Сегментационный тест: отправить N test-кадров         | 82           |
| 0x30 | FIFO TX тест: записать 188-байт блок                  | 11           |
| 0x31 | FIFO статус (vacancy, occupancy, ISR)                 | 18           |
| 0x32 | FIFO reset                                            | 3            |
| 0x40 | Watchdog stats (reboot_status, kick/starve count)     | 24           |
| 0x41 | Simulate hang (magic 0x5A) — для теста WDT            | 3            |
| 0x42 | Force SLCR reset (magic 0xA5) — минуя WDT/ISR         | 3            |

Подробности — см. `handle_proto_0x90` в `cmd_udp.c`. Watchdog-команды детально описаны в `WATCHDOG.md`.

### Дублирование `0x90` ↔ `0xA0`

Команды `0x90 0x02/0x03/0x04` (set TX/RX freq, TX atten) и эквивалентные cregs делают *похожее*, но не идентичное:

| Команда       | Единицы    | Куда пишет                              |
|---------------|------------|-----------------------------------------|
| `0x90 0x02`   | Hz (u64)   | только AD9361 SPI (no-OS API)           |
| `0xA0 g=1 c=2`| kHz (u32)  | AD9361 SPI **+ AXI creg PL**            |
| `0x90 0x04`   | dB (u32)   | только AD9361 SPI                       |
| `0xA0 g=1 c=3`| 250-mdB    | AD9361 SPI **+ AXI creg PL**            |

**Рекомендация для нового кода (Qt/host):** использовать `0xA0`. Старые Qt-программы могут продолжать работать через `0x90`.

---

## Legacy-протокол (ucnt + cmd)

Кроме `0x90` и `0xA0`, на тот же порт 60012 принимается ещё один формат — наследие от ещё более старой версии:

| Байты | Поле                        |
|-------|-----------------------------|
| 0..1  | ucnt (counter, big-endian)  |
| 2     | cmd                         |
| 3..   | payload                     |

Обрабатывается в `handle_legacy()`:

| cmd  | Длина | Назначение                                              |
|------|-------|---------------------------------------------------------|
| 0x05 | 11    | Set firewall config (mask + 4 enable bits)              |
| 0x06 | 3     | Get firewall config (echo + 11 байт ответа)             |
| 0x07 | 15    | Set bridge IP / mask / gateway (с ACK до смены)         |

Команда `0x07` отправляет ACK **до** смены IP, чтобы пакет ушёл со старого адреса. Это критично — без этого клиент не получит подтверждения.

---

## Инициализация при старте

Последовательность из `main()`:

1. `knk_init_platform()` — UART, GPIO, SCUGIC.
2. `init_tx_rx_fifo()`, `init_deb_fifo()` — AXI FIFO драйверы.
3. `AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, 0x00010000)` — ранний preset `ppos.creg2` (до `modem_init`!).
4. `spi_engine_ad9361_init()`, `ad9361_init(&default_init_param)` — поднимаем AD9361.
5. `spi_engine_npu_com_board_init()`, `spi_engine_npu_pow_board_init()` — конфигурим MAX7301 на ячейках NPU (с верификацией).
6. **Прямые SPI-команды в PPOS DSP** для par[]-регистров (master/slave specific) — историческая инициализация, постепенно мигрирует в cregs.
7. **`modem_init()`** — записывает все cregs в дефолтное состояние:
   - `ppos.creg2` (Tang=0, Tang_avt=0, GPIOnR_TYPE=1, для AntSDR — Rx1B/Tx1B/Rx2B/Tx2B=0)
   - `ppos.creg4` (txnrx_setup=2000, txnrx_hold=4500)
   - `tx_chan0.creg0` (mode 576, scr_on=1, mux_ad9361_din=5, run=0)
   - `tx_chan0.creg2` (3500 MHz, + ad9361_set_tx_lo_freq)
   - `tx_chan0.creg3` (40000 mdB / 250 = 160 единиц = 40 dB, + ad9361_set_tx_attenuation)
   - `rx_chan0.creg0` (mode 576, scr_on=1, rcos_filt_on=1, rot_on=1, run=0)
   - `rx_chan0.creg1` (3500 MHz, + ad9361_set_rx_lo_freq)
8. `firewall_init()`, `lwip_init()`, `net_init()` — сетевая часть.
9. UDP PCB на 60012 — поднимается `cmd_udp_pcb` с broadcast-флагом.
10. `platform_enable_interrupts()`, `is_run = true`, **главный цикл**.

В цикле при поступлении `0xA0`-пакета — `handle_creg_exch` обновит соответствующий creg в PL и (для AD9361 параметров) дёрнет no-OS API.

---

## Что требует уточнения у разработчика PL

Эти моменты нужно зафиксировать в документации после консультации:

1. **Direction отдельных битов `ppos.creg2`** — какие пишутся PS, какие читаются PL для статуса. Из текущего знания: V_TEMP, V_POWER читаются нами; Tang, Fdiap пишутся нами. Остальное — неизвестно точно.

2. **Что реально делает PL с `tx_chan0.creg2/3`, `rx_chan0.creg1`** — там хранятся параметры AD9361 в kHz/250-mdB. AD9361 настраивается через no-OS драйвер (SPI напрямую). Возможные варианты:
   - PL хранит копию для отчётности по UDP `0x90 0x10/0x11/0x12`
   - PL делает дополнительные коррекции (rotation, freq shift) на основе этих значений
   - PL частично перекрывает SPI-настройку через свой собственный путь

3. **Семантика `run` в `tx_chan0.creg0` / `rx_chan0.creg0`** — может ли PL сбрасывать его независимо от PS, и при каких условиях. Сейчас PS читает `run` и реагирует, но если PL никогда сам не пишет — это просто sticky-bit и логика lazy-restart избыточна.

4. **Существование документа «карта регистров»** от автора PL — если такой есть, его стоит положить в `docs/` и ссылаться на него.

5. **Версия PL (через `ppos.creg0`)** — какая текущая `pn/pv/sn`, и должен ли PS проверять совместимость при старте.
