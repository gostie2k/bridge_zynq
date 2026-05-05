# CLAUDE.md

Контекст для AI-ассистента (Claude Code) при работе с этим проектом. Прочитай этот файл перед тем как начинать любую задачу.

---

## Проект

PS-часть прошивки модема ПКРВ на Xilinx Zynq-7020. Bare-metal, lwIP 2.1.1, Vitis Classic 2023.2.

Делает: прозрачный Ethernet-мост (lwIP `bridgeif`) между внешним GEM и радиоканалом в PL. Управление модемом — через AXI control-регистры (cregs) и UDP-протоколы поверх `bridge_netif`.

**Главный документ для понимания архитектуры — `README.md`. Документ для понимания control plane и cregs — `docs/CONTROL_PLANE.md`. Прочитай оба перед нетривиальными изменениями.**

---

## Hardware

- SoC: Xilinx Zynq-7000 XC7Z020, dual Cortex-A9, используется CPU0
- RF: Analog Devices AD9361 через no-OS драйвер (`ad9361/`)
- PL: PPOS DSP IP (4 AXI slave: control regs, RX/TX core, два SPI engine)
- Внешние ячейки: COM-board (CS0) и POW-board (CS1) с MAX7301
- Ethernet: GEM0 (port1 моста)

Target-плата выбирается дефайнами в `platform_config.h`:
- `IS_ANTSDR` — основной target
- `IS_MAKET_PKRV` — макет ПКРВ
- `IS_ZED_BOARD` — Zedboard для отладки

Роль modem'а: `IS_MASTER` или `IS_SLAVE` (там же).

---

## Tooling

- **IDE**: Vitis Classic 2023.2 (xsct, arm-none-eabi-gcc)
- **Runtime**: bare-metal (standalone BSP), без RTOS
- **lwIP**: версия 2.1.1, RAW API (callback-based)
- **Файлы**: `*.c`, `*.h` (C99), стиль табуляция/пробелы — смешанный, не унифицирован
- **Логирование**: `xil_printf` (НЕ `printf` в hot path — `printf` тянет stdio и тяжелее)

---

## Структура проекта

Репо плоский — все исходники в корне, кроме no-OS драйверов в подпапках:

```
/
├── main.c                  — entry, init, main loop, ISR
├── bridge_func.c/h         — net_init(), vport3 для радиоканала
├── bridgeif_fdb.c          — наша версия FDB (форк lwIP contrib)
├── radio_stream.c/h        — сегментация/CRC32 кадров (продакшн TX/RX path)
├── firewall.c/h            — фильтр (сейчас НЕ врезан в data path)
├── cmd_udp.c/h             — UDP control plane: 0x90, legacy ucnt, 0xA0
├── modem.c/h               — modem_init, описания cregs, SPI NPU, lwIP stats
├── platform.c/h            — UART/GPIO/ScuGic init
├── platform_config.h       — карта адресов, целевая плата, master/slave
├── error_tracker.c/h       — счётчики Eth/FIFO для отладки
├── cmd_buf.c/h             — legacy путь (pkt_len=4800), для отладки
├── vid_buf.h               — RTP-видео буфер
├── fifo_test.c             — отладка AXI FIFO
├── no_os_*.c               — заглушки/адаптеры под no-OS драйвер AD9361
├── lscript.ld              — линкер скрипт
├── ad9361/                 — no-OS драйвер AD9361 (ADI), не править
├── axi_adc_core/           — no-OS ADC core, не править
├── axi_dac_core/           — no-OS DAC core, не править
├── axi_dmac/               — no-OS DMAC, не править
├── video/                  — обработка видео
└── docs/
    └── CONTROL_PLANE.md
```

---

## Что НЕ трогать

- `ad9361/`, `axi_adc_core/`, `axi_dac_core/`, `axi_dmac/` — это no-OS драйверы от Analog Devices, обновляются upstream'ом, локальные правки усложнят будущий update.
- `xparameters.h` — автогенерируется из hardware platform Vitis'ом, ручные правки сотрутся.
- BSP файлы (`*/bsp/*`) — автогенерируются.
- `bridgeif_fdb.c` — это форк lwIP contrib с локальными изменениями (отключенный таймер aging, добавленный `_first_fdb_src`); правки делать осознанно.

**Файлы, которые можно править свободно:** все `.c/.h` в корне репо, кроме перечисленного выше.

---

## Что НЕ трогать без обсуждения

- Битовые поля в `ppos_par`, `ppos_tx`, `ppos_rx` (в `modem.h`) — это контракт с PL. Изменение порядка битов сломает PL без видимых для PS симптомов кроме «модем не работает». Перед изменением — сверять с разработчиком PL и обновлять `docs/CONTROL_PLANE.md`.

- Преамбулы в `radio_stream.c` (`0xb6ce261d`, `0x6d9c`, `0xFACE`) — контракт с PL и форматом кадра соответственно. Менять — только синхронно с обоими концами.

- Размеры блоков (188 байт payload, 216 байт в FIFO с обвязкой) — фиксированный контракт. Если нужно менять — это серьёзный архитектурный change.

---

## Code conventions

- Имена функций: snake_case (`radio_linkoutput`, `firewall_filter`).
- Имена структур: snake_case (`stat_modem`, `radio_stats_t`).
- Битовые поля: snake_case в lowercase.
- Префиксы наших модулей: `bridge_`, `radio_`, `firewall_`, `modem_`, `cmd_`.
- Префикс `xil_` зарезервирован за Xilinx, `lwip_` за lwIP, `ad9361_` / `no_os_` за ADI.
- Константы и `#define` — UPPER_SNAKE.
- `xil_printf` для логов; форматтеры `%lu`, `%llu` поддерживаются ограниченно (см. документацию Xilinx). Для `uint64_t` — разбивать на два `%lu` для high/low half.
- Для bare-metal: НЕ использовать `malloc/free` в hot path. Динамическая аллокация только в init или редких событиях. lwIP `pbuf_alloc` — допустимо, это пуловая аллокация.

---

## Build / Test workflow

Сборка только через Vitis IDE (Project → Build All) или CLI:
```
xsct build.tcl    # если у тебя настроен скрипт
```

Прогон — через JTAG (debug session в Vitis) или `boot.bin`. UART0 — отладочный вывод, 115200.

**Нет CI**, нет автоматических тестов. Проверка изменений — ручная: собираешь, заливаешь, смотришь логи + работу в реальной сети + статус по `0x90 0x20`.

---

## Что важно знать про lwIP в этом проекте

- Используется RAW API (callback-based, без потоков).
- Главный цикл крутит `xemacif_input(echo_netif)` — это разгребает пакеты GEM в lwIP.
- Изменения в `lwipopts.h` (точнее в `opt.h`) нужно делать **во всех трёх копиях** (см. README — это проблема Vitis BSP).
- `LWIP_NUM_NETIF_CLIENT_DATA = 7` — bridgeif хранит state как client_data слоты на netif.
- `IP_SOF_BROADCAST` и `IP_SOF_BROADCAST_RECV` включены для broadcast PCB (60012).

---

## Что важно знать про bridgeif

- 3 порта: `port1_bridge` (xemac, GEM), `port3_bridge` (vport3, радио), `bridge_netif` (виртуальный, для control plane).
- FDB размер: 1024 entries, 16 static (см. `BRIDGEIF_INITDATA1` в `bridge_func.h`).
- promiscuous mode на GEM включается прямой записью в `XEMACPS_NWCFG_OFFSET` после `xemac_add()`.
- `vport3_linkoutput` — НЕ просто отправка в радио, а **роутер по `tx_chan0.creg0.pkt_len`** (576 vs 4800). Об этом надо помнить при правках.

---

## Известные грязные места

Они не баги в смысле «надо срочно чинить», но при работе рядом — учитывать:

- `firewall_filter` закомментирован в `vport3_linkoutput`. Если нужно врезать обратно — раскомментировать и проверить, что firewall настроен (init по умолчанию пропускает всё).
- В `bridge_func.c` много закомментированного кода про `port2_bridge`, `udp_netif2` — это исторический хвост, можно игнорировать или удалять при рефакторинге.
- В `IntrHandler` GPIO 54+63 большой блок видео-отправки закомментирован — заменён на `tx_process()` в main loop.
- `radio_stream.c.txt` рядом с `radio_stream.c` — старая копия, не используется.
- Дублирование 0x90 и 0xA0 для AD9361 параметров — оставлено сознательно для совместимости.

---

## Полезные подсказки для типовых задач

**Хочешь добавить новый AXI-регистр в PL?**
1. Добавь bit-field в `struct ppos_par/ppos_tx/ppos_rx` в `modem.h`.
2. Если это новый блок — добавь `*_PAR_OFFSET` define и обнови `AXI_CNTRL_REG_*` макросы при необходимости.
3. Расширь `handle_creg_exch` в `cmd_udp.c` для группы/creg.
4. Опиши в `docs/CONTROL_PLANE.md`.
5. Согласуй с разработчиком PL.

**Хочешь добавить UDP-команду для диагностики?**
- Если без AXI — добавь sub-cmd в `handle_proto_0x90` в `cmd_udp.c`.
- Если с AXI — лучше через `0xA0`.
- Помни: `LOG_DEBUG` управляет verbose'ностью (см. `cmd_udp.h`).

**Хочешь поменять формат радио-кадра?**
- Это ломает PL. Не делай в одиночку.
- Если нужно — менять `radio_stream.c` (TX и RX симметрично) и обязательно обновлять преамбулы и тесты.

**Хочешь добавить логирование?**
- Используй `xil_printf`. Для verbose-уровней — паттерн `LOG1`/`LOG2` из `cmd_udp.h`.
- НЕ оставляй `printf` в hot path (особенно в radio_stream и main loop).

**Получил странные потери пакетов?**
- Команда `0x90 0x20` — статистика. Смотри `err_seq_gap` (потеря 188-блоков), `err_pkt_gap` (потеря целых кадров), `err_crc` (битые кадры), `err_fifo_full` (PS не успевает писать в FIFO), `err_rx_overflow` (PS не успевает читать).

---

## Когда сомневаешься

- Архитектурные изменения, изменение протоколов (0x90/0xA0/legacy ucnt), форматов радио-кадров, AXI-регистров — **спроси перед тем как делать**.
- Локальный рефакторинг внутри функции, рефакторинг логирования, исправление опечаток, уточнение комментариев — делай смело.
- Удаление мёртвого кода (закомментированных блоков) — лучше делать отдельным коммитом и обсудить.

---

## Стиль ответов от Claude

- Отвечай по-русски, как и пользователь.
- Не пересказывай очевидное (что есть в README).
- Будь конкретным: указывай файл и строку, давай патч diff'ом или показывай только ту функцию, что меняется.
- Не предлагай рефакторинг там, где об этом не просили — просто решай задачу.
- Если видишь проблему рядом с тем что просили починить — упомяни одной строкой, не разворачивай.
