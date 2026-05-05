# bridge_zynq

PS-часть прошивки модема ПКРВ на Zynq-7020. Bare-metal, lwIP 2.1.1, IDE: Vitis Classic 2023.2.

Приложение реализует прозрачный Ethernet-мост между внешним сетевым портом (GEM, port1) и радиоканалом, организованным в PL. Управление и инициализация модема выполняются через AXI control-регистры (cregs); параллельно поддерживается legacy-протокол `0x90` поверх UDP для совместимости со старыми Qt-программами.

---

## Назначение

Узел в составе модема ПКРВ. Принимает Ethernet-кадры с внешнего порта, оборачивает их в транспортные единицы и нарезает на блоки фиксированной длины для передачи в PL (модулятор и AD9361). На приёме — обратная сборка кадров и инжекция обратно в bridge. Параллельно — управление модемом (частоты, аттенюация, GPIO ячеек НПУ) и сбор статистики.

```
┌─────────────────────────────────────────────────────────────────────┐
│                              PS (Cortex-A9)                         │
│                                                                     │
│   ┌──────────┐                                                      │
│   │ xemac    │ port1                                                │
│   │ (GEM)    │◄───┐                                                 │
│   └─────┬────┘    │                                                 │
│         │         │                                                 │
│         ▼         │                                                 │
│   ┌──────────────────┐    bridge_netif (192.168.250.x)              │
│   │   bridgeif       │◄─── UDP:60012 control plane                  │
│   │   (lwIP FDB)     │     (cmd_udp_callback)                       │
│   └─────┬────────────┘                                              │
│         │ port3_bridge                                              │
│         ▼                                                           │
│   ┌──────────────────┐                                              │
│   │ vport3_linkout   │  switch by tx_chan0.creg0.pkt_len:           │
│   │                  │   • 576  → radio_stream (продакшн)           │
│   │                  │   • 4800 → cmd_buf (legacy debug)            │
│   └─────┬────────────┘                                              │
│         │                                                           │
│         ▼  radio_linkoutput → tx_ring → tx_process                  │
│   ┌──────────────────┐                                              │
│   │ radio_stream.c   │  [MAGIC FACE | ID | LEN | DATA | CRC32]      │
│   │                  │   нарезка на блоки 188 байт                  │
│   └─────┬────────────┘                                              │
│         │                                                           │
│         ▼  send_block_to_pl                                         │
│   ┌──────────────────┐  216 байт = PL-обвязка + 188-байтный блок    │
│   │  AXI FIFO TX     │                                              │
│   └─────┬────────────┘                                              │
└─────────┼───────────────────────────────────────────────────────────┘
          │
┌─────────┼───────────────────────────────────────────────────────────┐
│         ▼                              PL (PPOS DSP)                │
│   модулятор → AD9361 (LVDS, FDD)                                    │
│         │                                                           │
│         ▼ обратный путь: AD9361 → демодулятор → AXI FIFO RX         │
└─────────┼───────────────────────────────────────────────────────────┘
          │
┌─────────┼───────────────────────────────────────────────────────────┐
│         ▼                                                           │
│   ┌──────────────────┐  drain_rx_fifo (для pkt_len==576)            │
│   │  AXI FIFO RX     │  читает кадры из PL, проверяет преамбулу     │
│   └─────┬────────────┘                                              │
│         ▼  radio_input_block → state machine → CRC32                │
│   ┌──────────────────┐                                              │
│   │  RX парсер       │  собирает кадры, валидирует, инжектирует     │
│   │  radio_stream.c  │  в port3_bridge.input → bridgeif → port1     │
│   └──────────────────┘                                              │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Hardware

- **SoC**: Xilinx Zynq-7000 XC7Z020 (плата AntSDR — основной target, см. `IS_ANTSDR` в `platform_config.h`)
- **RF**: Analog Devices AD9361 (no-OS драйвер ADI)
- **PL модулятор**: PPOS DSP IP (адресация через 4 AXI slave: control regs, RX/TX core, два SPI engine)
- **Внешние ячейки** (через SPI engine NPU): COM-board (CS0, MAX7301) и POW-board (CS1, MAX7301)
- **Ethernet**: GEM0 (port1 моста), promiscuous mode

Альтернативные target-платы: `IS_MAKET_PKRV`, `IS_ZED_BOARD` — переключение через дефайны в `platform_config.h`.

---

## Карта AXI

Все адреса — относительно `AXI_CNTRL_REG_BASEADDR = XPAR_PPOS_DSP_0_S00_AXI_BASEADDR`.

| Offset | Блок              | Что внутри                                        |
|--------|-------------------|---------------------------------------------------|
| 0x00   | `ppos.cregN`      | общие настройки (Tang, Fdiap, txnrx timing, …)    |
| 0x20   | `tx_chan0.cregN`  | TX канал (mode, частота, аттенюация)              |
| 0x30   | `rx_chan0.cregN`  | RX канал (mode, частота, фильтры)                 |

Полное описание полей и битов — `docs/CONTROL_PLANE.md`.

Дополнительные AXI slave PPOS DSP:
- `S01_AXI` (`RX_CORE_BASEADDR`, `TX_CORE_BASEADDR`) — AXI ADC/DAC core (no-OS)
- `S02_AXI` (`SPI_ENGINE_AD9361_BASEADDR`) — SPI engine для AD9361
- `S03_AXI` (`SPI_ENGINE_NPU_BASEADDR`) — SPI engine для COM/POW-board

Отдельные периферийные блоки:
- `XPAR_AXI_FIFO_MM_S_0_BASEADDR` — основной TX/RX FIFO в радиоканал
- `XPAR_AXI_FIFO_MM_S_1_BASEADDR` — debug FIFO

---

## Сетевая часть

### Bridge

`bridgeif` (lwIP contrib) с FDB. Активные порты:

| Порт          | netif                | назначение                     |
|---------------|----------------------|--------------------------------|
| port1_bridge  | xemac (GEM)          | внешний Ethernet               |
| port3_bridge  | vport3 (программный) | радиоканал (через radio_stream)|

`bridge_netif` — виртуальный интерфейс самого моста, имеет IP. На нём поднят UDP:60012 для control plane.

### IP-адреса

Master/Slave выбирается дефайном (`IS_MASTER` / `IS_SLAVE`):

| Роль   | bridge IP        | mac1 (port1)          | mac3 (port3)          |
|--------|------------------|-----------------------|-----------------------|
| Master | 192.168.250.11   | 00:1A:35:AA:BB:CC     | 00:1A:35:DD:EE:03     |
| Slave  | 192.168.250.21   | 00:0A:35:AA:BB:CC     | 00:0A:35:DD:EE:03     |

Маска `255.255.255.0`, gateway `192.168.250.1`. IP перенастраивается по UDP командой `0x07` (legacy).

### UDP-порты

| Порт  | PCB            | Что слушает / куда шлёт                          |
|-------|----------------|--------------------------------------------------|
| 60012 | cmd_udp_pcb    | control plane (см. `docs/CONTROL_PLANE.md`)      |
| 5008  | recv_pcb       | приём RTP-видео (push в `vid_buf`)               |
| 5018  | send_rx_data   | TX видеопакетов (RemotePortRx)                   |
| 5078  | debug_pcb      | TX отладочных пакетов (RemotePortDeb)            |
| 5007  | debug_pcb      | TX статуса модема (`stat_modem`)                 |

### Изменения в lwIP `opt.h`

⚠️ **Изменения нужно внести во все три копии `opt.h`** в проекте Vitis:

```
1. .../export/.../bspinclude/include/lwip/opt.h
2. .../bsp/.../include/lwip/opt.h
3. .../libsrc/lwip211_v1_7/src/lwip-2.1.1/src/include/lwip/opt.h
```

| Параметр                       | Default | Установлено | Зачем                              |
|--------------------------------|---------|-------------|------------------------------------|
| `LWIP_NUM_NETIF_CLIENT_DATA`   | 0       | **7**       | bridgeif хранит FDB и порты как client_data |
| `IP_SOF_BROADCAST`             | 0       | **1**       | разрешает broadcast PCB            |
| `IP_SOF_BROADCAST_RECV`        | 0       | **1**       | фильтрует broadcast при приёме     |

---

## Радиоканал (radio_stream.c)

Двухуровневая инкапсуляция кадров:

**Уровень 1 — radio_stream (PS):**
```
┌────────┬──────┬──────┬──────────┬────────┐
│ MAGIC  │  ID  │ LEN  │   DATA   │ CRC32  │
│ 0xFACE │ 2 b  │ 2 b  │ 0..1520b │  4 b   │
└────────┴──────┴──────┴──────────┴────────┘
                       │
                       ▼ нарезка на блоки 188 байт
┌──────┬─────────────────────────┐
│ seq  │       payload           │
│ 1 b  │       187 b             │
└──────┴─────────────────────────┘
```

**Уровень 2 — обвязка для PL:**
```
[0xb6ce261d] [0x6d9c << 16 | cnt_tx_seg] [0x00000000] [188 байт] [check] [00 00 00] = 216 байт в FIFO
```

То есть в FIFO уходит **216 байт** на блок, а не 188. Поверх 188-байтных блоков идёт «внешний» счётчик сегментов `cnt_tx_seg` (16 бит), проверяемый PL. Внутри 188 байт первый байт — независимый `seq` для radio_stream RX парсера.

CRC32 (стандартный, polynomial 0xEDB88320) считается по полю DATA только.

Размер кольцевых буферов TX/RX: `RING_SIZE = 188 * 512 = 96 256` байт.

### TX path

`radio_linkoutput(netif, p)` пишет хедер + payload + CRC в `tx_ring` (через `tx_push_block`, блочная запись с обходом round-trip), затем `tx_process()` достаёт по 187 байт payload, прибавляет байт `seq` и через `send_block_to_pl()` отправляет в FIFO с PL-обвязкой.

`radio_tx_periodic_flush()` дозабивает неполный последний блок нулями — вызывается раз за итерацию main loop, чтобы не зависал «хвост» в ring.

### RX path

`drain_rx_fifo()` ждёт от PL пакеты длиной 54 32-битных слова (216 байт), проверяет преамбулу `0xb6ce261d / 0x6d9c | cnt_seg_in`, проверяет 16-битную сумму и передаёт 188 байт payload в `radio_input_block()`.

`radio_input_block()` — конечный автомат (`ST_MAGIC_H` → `ST_MAGIC_L` → `ST_ID_H/L` → `ST_LEN_H/L` → `ST_DATA` → `ST_CRC0..3`), собирает кадр в `rx_pkt_buffer`, проверяет CRC32 и при успехе делает `pbuf_alloc + memcpy + port3_bridge.input()`.

### Счётчики и статистика

`radio_stats_t` (получить — `radio_stream_get_stats`):

| Поле              | Что считает                              |
|-------------------|------------------------------------------|
| `tx_pkts`         | Принятые от bridge кадры                 |
| `tx_blocks`       | Отправленные в FIFO 188-блоки            |
| `err_fifo_full`   | TX FIFO полон, блок дропнут              |
| `rx_blocks`       | Принятые от PL 188-блоки                 |
| `err_seq_gap`     | Разрыв `seq` (потеря сегмента)           |
| `err_rx_overflow` | Переполнение `rx_ring`                   |
| `rx_pkts`         | Успешно собранные кадры (CRC OK)         |
| `err_pkt_gap`     | Разрыв packet ID                         |
| `err_crc`         | Кадр пришёл, CRC не сошёлся              |

Доступ через UDP: команда `0x90 0x20`. Также есть `dbg_err_tx_stall` и `dbg_err_tx_reject` (ring переполнен на TX) — пока во внешний API не выводятся.

---

## Control plane

Управление модемом: AD9361, PPOS DSP cregs, NPU ячейки, firewall, IP-адрес.

**Текущая стратегия:** инициализация и runtime-управление — через AXI cregs (UDP-протокол `0xA0`). Legacy-протокол `0x90` оставлен для совместимости со старыми Qt-программами и для функций, не покрытых cregs (NPU, fastlock, GPIO mask, диагностика).

Полное описание — `docs/CONTROL_PLANE.md`.

---

## Firewall

`firewall.c/h` — фильтр для исходящего трафика на port3 (Ethernet → радио). Поля:
- `ip_mask` — фильтрация по dst IP относительно bridge IP (0 = не фильтровать)
- `arp_en`, `icmp_en`, `tcp_en`, `udp_en` — пропуск по EtherType / IP protocol

⚠️ **Сейчас вызов `firewall_filter` в `vport3_linkoutput` закомментирован**, фильтрация в data path неактивна. Управление настройками работает (UDP команда `0x05`/`0x06`), сами настройки хранятся.

---

## Файловая структура

```
/
├── main.c                  — entry point, init, main loop, ISR
├── bridge_func.c/h         — net_init(), vport3_init/linkoutput, FDB утилиты
├── bridgeif_fdb.c          — реализация FDB (форк lwIP contrib + наша aging)
├── radio_stream.c/h        — сегментация/сборка кадров, CRC32
├── firewall.c/h            — пакетный фильтр (выключен в data path)
├── cmd_udp.c/h             — обработчики 0x90, legacy ucnt, 0xA0
├── modem.c/h               — modem_init(), описание cregs, SPI NPU, статистика lwIP
├── platform.c/h            — UART/GPIO/ScuGic init, Xilinx-обвязка
├── platform_config.h       — карта адресов, target-платы, master/slave
├── error_tracker.c/h       — счётчики Eth/FIFO для отладки
├── cmd_buf.c/h             — legacy режим (pkt_len=4800), кольцевой буфер cmd_pkt
├── vid_buf.h               — буферизация RTP-видео
├── fifo_test.c             — отладка работы AXI FIFO
├── no_os_*.c               — заглушки/адаптеры под no-OS AD9361 драйвер
├── lscript.ld              — линкер скрипт (DDR, OCM, default Xilinx)
├── ad9361/                 — no-OS драйвер AD9361 (ADI)
├── axi_adc_core/           — no-OS ADC core
├── axi_dac_core/           — no-OS DAC core
├── axi_dmac/               — no-OS DMA controller
├── video/                  — обработка видео-пакетов
└── docs/
    └── CONTROL_PLANE.md    — карта регистров, протоколы 0x90 и 0xA0
```

---

## Сборка и запуск

1. Открыть проект в Vitis Classic 2023.2.
2. Убедиться, что подключена правильная hardware platform (`.xsa` от PL).
3. Внести правки в `lwip/opt.h` (см. выше) — **во все три копии**.
4. В `platform_config.h` выбрать роль (`IS_MASTER` / `IS_SLAVE`) и target-плату.
5. Project → Build All.
6. Прошивка через JTAG или загрузка по boot.bin (BootROM конфигурирует UART на 115200).

UART0 (PS UART) — отладочный вывод, baud 115200.

---

## Режимы работы (`tx_chan0.creg0.pkt_len`)

Поле `pkt_len` в `creg0` TX-канала играет роль селектора режима:

| Значение | Режим                  | Code path TX                          | Code path RX           |
|----------|------------------------|---------------------------------------|------------------------|
| 576      | radio_stream (актуальный) | `radio_linkoutput` → `tx_process`     | `drain_rx_fifo` → `radio_input_block` |
| 4800     | legacy debug           | `cmd_push` в `buf_cmd` → `send_cmd`   | `fifo_rx_buf_write` (старый формат, custom-преамбула + 450-словный пакет) |

**Используется режим 576.** Режим 4800 оставлен для отладки и обратной совместимости со старыми компоновками PL/PS.

---

## Известные нюансы и TODO

- ⚠️ `firewall_filter` не врезан в data path (`vport3_linkoutput`) — закомментирован.
- ⚠️ Приём global broadcast `255.255.255.255` пока не работает; subnet broadcast (`192.168.250.255`) — работает.
- ⚠️ Файл `radio_stream.c.txt` — старая копия в репо, не используется при сборке. Можно удалить.
- В `bridge_func.c/h` остаются закомментированные `port2_bridge`, `udp_netif2`, `mac2`, `mac4` — это исторический хвост от старой архитектуры.
- В `IntrHandler` GPIO `54+63` бóльшая часть кода закомментирована (отправка vid/cmd-пакетов в FIFO в старом формате) — функционал перенесён в `radio_stream.c`.
- Видеотракт (`vid_buf`, `recv_callback` для RTP на порту 5008) частично активен на приём, отправка в радио в новой архитектуре через `vid_buf_exch` закомментирована в main loop.
- Direction некоторых битов в `ppos.creg2` (что пишем мы, что читает PL для статуса) — **требует уточнения у разработчика PL**. Известно: V_TEMP / V_POWER читаются нами; Tang, Fdiap пишутся нами.
- Дублирование настройки AD9361 в `creg2/creg3` для TX и `creg1` для RX — параллельно идёт `AXI_CNTRL_REG_WRITE` в PL и вызов `ad9361_set_*_lo_freq` через no-OS драйвер. **Какой канал реально настраивает RF — требует уточнения у разработчика PL** (предположительно SPI идёт через no-OS, PL хранит копию для статуса).

---

## Связанные документы

- `docs/CONTROL_PLANE.md` — описание control plane: cregs, протоколы 0xA0 и 0x90, переход с 0x90 на cregs.
- `CLAUDE.md` — контекст для AI-ассистента (Claude Code).
