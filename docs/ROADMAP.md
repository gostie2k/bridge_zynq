# План доработки прошивки PS-части модема ПКРВ

Документ для пошагового выполнения работ от текущего состояния до версии **v1.0.0**.
Читать сверху вниз. Каждый этап имеет цель, критерии готовности и риски.
Между этапами — коммит/тег, чтобы можно было откатиться.

---

## Версии и теги

| Тег        | Содержимое                                                |
|------------|-----------------------------------------------------------|
| v0.9.0     | Текущее состояние перед началом работ                     |
| v0.9.1     | Логгер внедрён                                            |
| v0.9.2     | Watchdog работает                                         |
| v0.9.3     | QSPI storage + config struct (без runtime смены роли)     |
| v0.9.4     | Live re-init + UDP команды смены конфига                  |
| v0.9.5     | Чистка мёртвого кода                                      |
| v0.9.6     | main.c разделён на модули                                 |
| **v1.0.0** | Версионирование, heartbeat, boot reason — релиз            |

---

## Этап 1 — Логгер (LOG_*)

**Цель:** заменить хаос из `xil_printf`/`printf` на единый макрос-логгер с уровнями
и тегами модулей. Паттерн — расширение того, что уже сделано в `cmd_udp.h`.

### 1.1. Создать `log.h`

```c
#ifndef LOG_H_
#define LOG_H_

#include "xil_printf.h"

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERR   1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_TRACE 5

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define _LOG(lvl, tag, fmt, ...)                                       \
    do { if ((lvl) <= LOG_LEVEL)                                       \
         xil_printf("[" tag "] " fmt "\r\n", ##__VA_ARGS__); } while(0)

#define LOG_E(tag, fmt, ...) _LOG(LOG_LEVEL_ERR,   tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) _LOG(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) _LOG(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) _LOG(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_T(tag, fmt, ...) _LOG(LOG_LEVEL_TRACE, tag, fmt, ##__VA_ARGS__)

#endif
```

### 1.2. Согласовать теги по модулям

| Тег       | Модуль                            |
|-----------|-----------------------------------|
| `BOOT`    | старт системы, init sequence      |
| `CFG`     | config storage, re-init           |
| `WDT`     | watchdog                          |
| `CMD`     | cmd_udp (0x90, 0xA0, legacy)      |
| `RADIO`   | radio_stream (TX/RX блоки, CRC)   |
| `MODEM`   | modem_init, cregs, статистика     |
| `BRIDGE`  | bridge_func, FDB                  |
| `FW`      | firewall                          |
| `SPI`     | SPI engines, AD9361, NPU          |
| `UART`    | UART command channel              |
| `NET`     | lwIP-общие, netif                 |
| `DIAG`    | диагностика 0x90 0x20/0x22/0x30…  |

### 1.3. Заменить вызовы

Поиск и замена:
- `xil_printf("[CMD] …")` → `LOG_I("CMD", …)` (убрать `\r\n` в конце — он в макросе).
- `printf("…")` → `LOG_I("…")` с подходящим тегом. `printf` вообще не должно остаться.
- Существующие `LOG1`/`LOG2` в `cmd_udp.c` оставить или переписать на `LOG_D`/`LOG_T` — на выбор.

Что **НЕ** трогать:
- `xil_printf` внутри `print_dynamic_fdb()` — это интерактивный дамп, не лог.
- Сообщения про FAIL/OK в `max7301_wr_v()` — это специальная отладочная функция.

### 1.4. Critical hot path
В `radio_input_block`, `radio_linkoutput`, `send_block_to_pl` — оставить
**только `LOG_T`** (TRACE). По дефолту они компилируются в no-op. Иначе при включении
INFO забьётся UART.

### Готово, когда:
- В коде нет `printf` нигде.
- Прошивка собирается с `LOG_LEVEL=LOG_LEVEL_INFO` и работает.
- Прошивка собирается с `LOG_LEVEL=LOG_LEVEL_TRACE` — поток логов читаемый, видны теги.

### Риск:
- `xil_printf` не поддерживает `%lu`/`%llu` полноценно. Проверь, что после замены 32/64-битные значения печатаются как раньше.

---

## Этап 2 — Watchdog (SCU)

Делаешь самостоятельно. Здесь только заметки на стыки:

### 2.1. Точка пинка
`wdt_kick()` в main loop, в самом конце итерации, ПОСЛЕ `xemacif_input()`. Так
будет ловиться зависание lwIP rx-обработки.

### 2.2. Перед re-init
В начале `device_reinit()` пнуть WDT, чтобы re-init имел запас по времени.
Если re-init дольше WDT timeout — увеличить timeout на время процедуры
(`XScuWdt_LoadWdt` с большим значением, потом обратно).

### 2.3. Boot reason
При старте читать `REBOOT_STATUS_REG` (`0xF8000258`), логировать причину
прошлого ребута:
```c
uint32_t rs = Xil_In32(0xF8000258);
LOG_I("BOOT", "reboot_status=0x%08X", rs);
Xil_Out32(0xF8000258, 0);  // clear для следующего раза
```
Декодировать биты по TRM — там есть POR, SLC_RST, DBG_RST, WDT_RST и т.д.

### Готово, когда:
- WDT сбрасывает CPU, если main loop завис.
- Boot reason пишется в лог при каждом старте.

---

## Этап 3 — QSPI storage + config struct

**Цель:** вынести роль и сетевые параметры в runtime-структуру в RAM,
которая загружается из QSPI при старте.

### 3.1. Структура конфига

В новом `device_config.h`:
```c
#define CFG_MAGIC    0x50504E4B  /* "PPNK" — ПКРВ */
#define CFG_VERSION  1

typedef enum {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1,
    ROLE_RELAY  = 2,   /* зарезервировано на будущее */
} role_t;

struct device_config {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t seq;             /* монотонный, актив = больший */

    uint8_t  role;            /* role_t */
    uint8_t  reserved1[3];

    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];

    uint8_t  mac1[6];         /* GEM (port1) */
    uint8_t  mac3[6];         /* radio (vport3) */
    uint8_t  bridge_mac[6];

    uint32_t serial;          /* для broadcast-set-ip */

    uint8_t  reserved2[64];   /* запас под будущие поля */

    uint32_t crc32;           /* по всей структуре выше */
};
```

`reserved2[64]` нужно, чтобы добавлять поля без бампа `version`. CRC считается по
полям magic..reserved2.

### 3.2. QSPI layout

Решить, где в QSPI выделить место. Зависит от размера флэша (16 или 32 MB) и
текущего boot.bin. Безопасный план:
- последний сектор флэша **минус 4 KB** = `CFG_SLOT_A`
- последний сектор флэша = `CFG_SLOT_B`

Размер сектора QSPI обычно 64 KB. Под конфиг хватает 4 KB на слот, поэтому
4 KB-erase достаточен (если чип поддерживает). Иначе erase 64K-сектор целиком.

```c
#define CFG_FLASH_SIZE       (16 * 1024 * 1024)   /* уточнить */
#define CFG_SECTOR_SIZE      (64 * 1024)
#define CFG_SLOT_A_OFFSET    (CFG_FLASH_SIZE - 2 * CFG_SECTOR_SIZE)
#define CFG_SLOT_B_OFFSET    (CFG_FLASH_SIZE - 1 * CFG_SECTOR_SIZE)
```

### 3.3. API `config_storage.h`

```c
typedef enum {
    CFG_LOAD_OK_FROM_FLASH = 0,
    CFG_LOAD_USED_DEFAULTS = 1,
    CFG_LOAD_RECOVERED_B   = 2,  /* slot A битый, взяли B */
    CFG_LOAD_RECOVERED_A   = 3,
} cfg_load_result_t;

cfg_load_result_t config_load(struct device_config *cfg);
int               config_save(const struct device_config *cfg);
void              config_set_defaults(struct device_config *cfg);
```

### 3.4. Алгоритм load
1. Прочитать slot A и slot B.
2. Проверить magic + CRC в обоих.
3. Если оба валидны → выбрать с большим `seq`. Если другой слот битый или с
   меньшим seq → переписать его.
4. Если валиден один → взять его, переписать второй.
5. Если ни одного → `config_set_defaults`, записать в оба слота.

### 3.5. Алгоритм save (atomic)
1. Определить неактивный слот (с меньшим seq или битый).
2. Erase сектор.
3. Записать новый конфиг с `seq = active.seq + 1`, CRC пересчитан.
4. Прочитать обратно, сверить CRC байт в байт.
5. Если ок → готово, он автоматически становится активным по seq.
6. Если не ок → старый слот цел, продолжаем работать с ним.

### 3.6. Defaults
```c
void config_set_defaults(struct device_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic   = CFG_MAGIC;
    cfg->version = CFG_VERSION;
    cfg->seq     = 1;
    cfg->role    = ROLE_MASTER;     /* стартуем master'ом */

    /* IP, MAC — как сейчас в коде для master'а */
    cfg->ip[0]=192; cfg->ip[1]=168; cfg->ip[2]=250; cfg->ip[3]=11;
    cfg->netmask[0]=255; cfg->netmask[1]=255; cfg->netmask[2]=255; cfg->netmask[3]=0;
    cfg->gateway[0]=192; cfg->gateway[1]=168; cfg->gateway[2]=250; cfg->gateway[3]=1;

    uint8_t m1[]={0x00,0x1A,0x35,0xAA,0xBB,0xCC};
    uint8_t m3[]={0x00,0x1A,0x35,0xDD,0xEE,0x03};
    uint8_t mb[]={0x00,0x1A,0x35,0xAA,0xBB,0xCE};
    memcpy(cfg->mac1, m1, 6);
    memcpy(cfg->mac3, m3, 6);
    memcpy(cfg->bridge_mac, mb, 6);

    cfg->serial = 0;   /* присвоить уникальный при первой прошивке */
    /* CRC посчитать в config_save перед записью */
}
```

### 3.7. Реализация QSPI I/O

Использовать `xqspips`. Базовый skeleton:
```c
static XQspiPs qspi_inst;

int qspi_init(void)
{
    XQspiPs_Config *cfg = XQspiPs_LookupConfig(XPAR_XQSPIPS_0_DEVICE_ID);
    if (XQspiPs_CfgInitialize(&qspi_inst, cfg, cfg->BaseAddress) != XST_SUCCESS)
        return -1;
    XQspiPs_SetOptions(&qspi_inst, XQSPIPS_MANUAL_START_OPTION |
                                   XQSPIPS_FORCE_SSELECT_OPTION |
                                   XQSPIPS_HOLD_B_DRIVE_OPTION);
    XQspiPs_SetClkPrescaler(&qspi_inst, XQSPIPS_CLK_PRESCALE_8);
    return 0;
}

int qspi_read(uint32_t addr, void *buf, size_t len);
int qspi_erase_sector(uint32_t addr);
int qspi_write(uint32_t addr, const void *buf, size_t len);
```

Чтение/запись — через `XQspiPs_PolledTransfer`. Полные примеры есть в Vitis
template `xqspips_flash_polled_example.c`. Скопируй оттуда, адаптируй.

### Готово, когда:
- `config_load()` корректно возвращает дефолты на чистом флэше и записывает их.
- `config_save()` + `config_load()` round-trip работает.
- При обрыве записи (имитировать через debug — сделать save, прервать после erase
  и перед записью) загрузка возвращает старый конфиг из второго слота.
- Глобальные `mac1/mac3`, IP в `net_init()` читаются из `current_config`, а не из
  компайл-тайм дефайнов. **На этом шаге `IS_MASTER/IS_SLAVE` ещё работают как
  источник defaults — это нормально.**

### Риск:
- Конфликт с `boot.bin`. Перед началом — узнать у тех, кто собирает boot.bin,
  размер итогового образа. Если близко к концу флэша — выбрать другое место.
- Erase 4K vs 64K. Если чип не поддерживает 4K-erase, придётся erase'ить весь
  64K-сектор, и слоты тогда займут 128K вместо 8K. Это не критично.

---

## Этап 4 — Live re-init

**Цель:** заменить compile-time `#ifdef IS_MASTER/IS_SLAVE` на runtime-конфиг.
Возможность сменить роль/IP по UDP без перезагрузки.

### 4.1. Удалить compile-time зависимости от роли

Места, где есть `#ifdef IS_MASTER/IS_SLAVE`:

- **`bridge_func.h`** — `bridge_data` и таблицы `mac1..mac4`. Сделать `bridge_data`
  не `static const`, заполнять из `current_config` в `net_init()`. MAC-таблицы
  удалить, читать из конфига.
- **`bridge_func.c`** — `net_init()`: IP-адреса.
- **`main.c`** — `RemoteAddr` (адрес ответчика); init par-регистров PPOS DSP
  (`0x84 0x08 0xc0/0x40` и далее — разные значения для master/slave).

Завести функцию:
```c
void apply_role_specific_init(role_t role);
```
которая делает SPI-конфигурацию par-регистров под нужную роль. Вызывается из
`device_reinit()` и из основного init после `config_load()`.

### 4.2. `device_reinit()`

```c
int device_reinit(const struct device_config *new_cfg)
{
    LOG_I("CFG", "reinit start, role=%d ip=%d.%d.%d.%d",
          new_cfg->role,
          new_cfg->ip[0], new_cfg->ip[1], new_cfg->ip[2], new_cfg->ip[3]);

    /* 1. Заморозить data path */
    is_run = false;
    Xil_ExceptionDisable();

    /* 2. Остановить cregs (PL перестанет дёргать FIFO) */
    tx_chan0.creg0.inside.run = 0;
    rx_chan0.creg0.inside.run = 0;
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0,
                        tx_chan0.creg0.word);
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0,
                        rx_chan0.creg0.word);

    /* 3. Сбросить FIFO */
    XLlFifo_Reset(&fifo);
    XLlFifo_IntClear(&fifo, 0xFFFFFFFF);

    /* 4. Сбросить radio_stream state */
    radio_stream_init();
    radio_stream_reset_stats();
    radio_stream_reset_rx_ids();

    /* 5. Закрыть UDP PCB */
    udp_remove(cmd_udp_pcb);   cmd_udp_pcb = NULL;
    udp_remove(recv_pcb);      recv_pcb = NULL;
    udp_remove(send_rx_data);  send_rx_data = NULL;
    udp_remove(debug_pcb);     debug_pcb = NULL;

    /* 6. Снести netif'ы */
    netif_set_down(&port1_bridge);
    netif_set_down(&port3_bridge);
    netif_set_down(&bridge_netif);
    netif_remove(&port1_bridge);
    netif_remove(&port3_bridge);
    netif_remove(&bridge_netif);

    /* 7. Применить новый конфиг к глобалам */
    current_config = *new_cfg;

    /* 8. Заново par-регистры PPOS DSP под новую роль */
    apply_role_specific_init(new_cfg->role);

    /* 9. AD9361 — НЕ переинициализируем, оставляем как есть */

    /* 10. Заново сетевой стек (читает current_config) */
    net_init();

    /* 11. Заново UDP listeners */
    init_udp_listeners();

    /* 12. Заново cregs */
    modem_init();

    /* 13. Запуститься */
    Xil_ExceptionEnable();
    is_run = true;

    LOG_I("CFG", "reinit done");
    return 0;
}
```

### 4.3. Подводные камни

| Место                 | Что сделать                                       |
|-----------------------|---------------------------------------------------|
| `bridge_data` const   | Снять `const`, заполнять в `net_init()`           |
| Promiscuous GEM       | Повторить запись в `XEMACPS_NWCFG_OFFSET` после `xemac_add()` (уже сейчас в `net_init`, проверить что вызывается при re-init) |
| `GEM MAC`             | `xemac_add()` пишет MAC в GEM-регистр. Если на повторе будут странности — `XEmacPs_Stop` + `XEmacPs_Reset` явно до `xemac_add` |
| `IntrHandler` GPIO63  | Уже проверяет `is_run` — ок                       |
| pending pbufs         | После `netif_remove` они автоматически дропнутся   |
| статика в `radio_stream.c` | `radio_stream_init()` обнуляет ring buffers, но global counters (`global_tx_pkt_counter`, `cnt_tx_seg`) — добавить сброс в `radio_stream_init()`, не только в `reset_stats()` |

### 4.4. Init UDP listeners вынести в функцию

Сейчас в `main()` блок про `recv_pcb` / `send_rx_data` / `debug_pcb` / `cmd_udp_pcb`.
Вынести в `init_udp_listeners()`, которая вызывается из `main` и из `device_reinit`.

### Готово, когда:
- Сборка без `IS_MASTER/IS_SLAVE` define'ов.
- В `platform_config.h` от них следов нет.
- При старте конфиг загружается из QSPI и применяется.
- На свежем флэше — defaults применяются, IP `192.168.250.11`, как раньше для master.

### Риск:
- GEM может не подняться после re-init. Митигация: если `device_reinit` упал,
  логировать и **уйти в watchdog reset** (просто бесконечный цикл без `wdt_kick`).
  К этому моменту WDT уже работает (этап 2).

---

## Этап 5 — UDP-команды смены конфига

**Цель:** удалённое управление ролью и сетевыми параметрами.

### 5.1. Расширить `0xA0` группой `device` (group=3)

| group | назначение           |
|-------|----------------------|
| 1     | tx_chan0 (как было)  |
| 2     | rx_chan0 (как было)  |
| **3** | **device (новое)**   |

### 5.2. Subcmd'ы для group=3

| Subcmd | Длина payload | Что делает                                  |
|--------|---------------|---------------------------------------------|
| 0x00   | 1             | set role (0=master, 1=slave)                |
| 0x01   | 12            | set network (ip[4]+mask[4]+gw[4])           |
| 0x02   | sizeof(cfg)   | set full config (через write+CRC проверка)  |
| 0x10   | 0             | get current config → ответ с конфигом       |
| 0x20   | 0             | reboot (через WDT — просто `while(1);`)     |
| 0x30   | 4+12          | broadcast set-ip by serial: target_serial[4]+ip[4]+mask[4]+gw[4] |

### 5.3. Алгоритм обработки set-команды

1. Валидировать payload (роль в диапазоне, IP не `0.0.0.0`, не multicast).
2. Скопировать текущий `current_config` в локальную `new_cfg`.
3. Применить изменения из команды к `new_cfg`.
4. Отправить ACK по UDP **до** re-init (как сейчас в `0x07`).
5. `device_reinit(&new_cfg)`.
6. Если успех → `config_save(&new_cfg)`.
7. Если re-init упал → broken state → watchdog reset.

### 5.4. Broadcast set-ip (subcmd 0x30)

Для случая «устройство имеет defaults IP, в сети уже есть конфликт или мы не
знаем IP». Команда приходит по broadcast (255.255.255.255 или 192.168.250.255).
- Проверить `target_serial == current_config.serial`. Если нет — игнорировать.
- Применить новые сетевые параметры, послать ACK уже с нового адреса.

Требует `IP_SOF_BROADCAST_RECV=1` в `lwipopts.h` — уже включено.

### Готово, когда:
- Команда `0xA0 g=3 c=0x00` меняет роль, видна по UART-логу, конфиг сохранён.
- После ребута роль остаётся прежней (загрузилась из QSPI).
- Команда `0xA0 g=3 c=0x01` меняет IP, ACK приходит со старого, дальше отвечает с нового.
- Broadcast set-ip работает.

---

## Этап 6 — Чистка мёртвого кода

**Цель:** удалить всё, что не используется, отдельным коммитом. До рефакторинга
структуры. Это снижает риск конфликтов на следующем этапе.

### 6.1. Файлы

- Удалить `radio_stream.c.txt` (старая копия рядом с боевым файлом).

### 6.2. `IntrHandler` в `main.c`

Удалить большие закомментированные блоки:
- блок про «MASTER размер 4800, 450 слов» с обвязкой preamble
- блок про «SLAVE размер 2250»
- блок TX-only debug

Оставить только активные ветки (GPIO 60, 61, 63 и 59).

### 6.3. `bridge_func.c`

Удалить:
- `vport2_init`, `vport2_linkoutput` (закомментированы)
- `udpif2_init`, `udpif2_linkoutput`, `eth_inp` (закомментированы)
- `port2_bridge`, `udp_netif2`, `mac2`, `mac4` упоминания
- закомментированный блок про `udp_netif2` в `net_init`

### 6.4. `bridge_func.h`

Удалить прототипы `vport2_*`, `udpif2_*`, `eth_inp`. Удалить mac2/mac4.

### 6.5. `cmd_udp.h`

- Удалить старый закомментированный заголовок в начале файла (`//#pragma once …`)
- Убрать прототип `handle_creg_exch` из `.h` (он static в `.c`, в `.h` ему не место).

### 6.6. `main.c` — мёртвые структуры и функции

Удалить:
- `struct krt_gr_dat` (закомментирована, на её месте `vid_pkt`)
- `struct cmd_pkt` (закомментирована, в `cmd_buf.h` лежит реальная)
- `struct cmd_vid`, `struct cmd_tel` (закомментированы)
- `send_debug_fifo_krt_gr_dat()` / `_empty()` (закомментированы)
- `send_debug_rx_check()` (закомментирована)
- старый вариант `fifo_rx_buf_write` без аргумента `cmd_rx_buf` (закомментирован)
- закомментированный `uart_cmd_chan` (старая версия)
- большой блок про rssi/debug в `IntrHandler` GPIO 60 (закомментирован)
- блок `fastlock store profile loop` (закомментирован)
- закомментированные `spi_engine_npu_pow_board_*` функции в `modem.c`
- `// printf("…")` блоки в status-выводе main loop

### 6.7. Закомментированные `xil_printf` в `bridge_func.c`

Все `if (DEB_FUNC)` блоки — заменить на `LOG_T("BRIDGE", …)`, `DEB_FUNC` удалить.

### 6.8. radio_stream.c — кракозябры

В комментариях есть битый юникод (`Р Р°Р·РјРµСЂ Р±Р»РѕРєР°` и т.д. — должно быть «Размер блока»).
Заменить на нормальные русские/английские комментарии.

### Готово, когда:
- В коде нет ни одного закомментированного блока кода больше 3 строк.
- Все `DEB_FUNC` ушли.
- Сборка проходит без warning'ов про unused functions.

### Риск:
- Удалить что-то, что на самом деле использовалось через `extern` из другого
  файла. Митигация: каждый удалённый блок прогнать `grep` по всему проекту перед
  удалением.

---

## Этап 7 — Разделение main.c

**Цель:** разбить `main.c` на тематические модули.

### 7.1. План разбиения

| Новый файл               | Что переносим                                                |
|--------------------------|--------------------------------------------------------------|
| `ad9361_config.c/h`      | `default_init_param`, `rx_fir_config`, `tx_fir_config`       |
| `isr.c/h`                | `IntrHandler` и связанные вспомогательные                    |
| `uart_cmd.c/h`           | `uart_cmd_chan`, `process_cmd_3bytes`, `send_response_byte`, `debug_ram_load`, `exec_func`, `func_type/func_par/func_start` |
| `status_send.c/h`        | `send_modem_status`, `send_deb`, периодический сбор температуры/RSSI/GPIO раз в секунду |
| `video_path.c/h`         | `recv_callback` (RTP-приём на 5008), `send_vid_packet_sized`, `IsRTTP`, vid_out обработка |
| `legacy_path.c/h`        | `send_cmd()`, `fifo_rx_buf_write()` (для pkt_len=4800), `cmd_buf_exch` integration. Помечено как deprecated, удалить в v1.1 |
| `fifo_debug.c/h`         | `fifo_deb_read`, `send_deb`, `deb_pkt_buf` |
| `net_setup.c/h`          | `init_udp_listeners()`, swap16/swap32 utils |

### 7.2. Что остаётся в `main.c`

Только:
- `int main(void)` — entry
- Init sequence (по шагам, вызовы из других модулей)
- Главный цикл с понятным разбиением на блоки

Цель: ≤300 строк в `main.c`.

### 7.3. Глобалы — куда?

Сейчас в `main.c` валяется куча `extern`-переменных. Перекинуть туда, куда они
логически принадлежат:
- `tx_chan0`, `rx_chan0`, `ppos`, `ad9361_phy` — в `modem.c` (декларации) +
  объявления в `modem.h` через `extern`.
- `fifo`, `fifo_deb` — в `fifo_debug.c`.
- `func_type/par/start` — в `uart_cmd.c`.
- `current_cmd_source`, `global_reply_pcb`, `last_client_ip/port` — в `uart_cmd.c`
  или `cmd_udp.c` (зависит от того, кто из них первым пишет).
- `is_run` — в `main.c` (это глобальный флаг состояния).
- `current_config` — в `device_config.c`.

### 7.4. Чинить `extern` в `.c`

В `cmd_udp.c` сейчас:
```c
extern u8 func_type;
extern u8 func_par[16];
extern bool func_start;
```
прямо внутри case. Это должно стать `#include "uart_cmd.h"` сверху файла.
Аналогично везде.

### Готово, когда:
- `main.c` ≤ 300 строк.
- Нет `extern`-объявлений переменных внутри `.c` файлов — всё через хедеры.
- Сборка проходит, поведение не изменилось.

### Риск:
- При переносе функций сломать ABI/линковку. Митигация: переносить по одному
  модулю, после каждого — собирать и прогонять `0x90 0x20` чтобы убедиться, что
  основные подсистемы живы.

---

## Этап 8 — Финальные штрихи (v1.0.0)

### 8.1. `version.h`

```c
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0

/* Заполняется через xsct-скрипт перед сборкой */
#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

#define FW_BUILD_DATE __DATE__ " " __TIME__
```

Скрипт `build.tcl` пусть генерирует header с git short hash и передаёт через `-D`.

### 8.2. Команда `0x90 0x01` — get version info

| Поле       | Тип       | Что            |
|------------|-----------|----------------|
| major      | u8        | 1              |
| minor      | u8        | 0              |
| patch      | u8        | 0              |
| reserved   | u8        |                |
| git_hash   | char[12]  | "abc1234"      |
| build_date | char[24]  | "Nov  1 2025…" |
| role       | u8        |                |
| serial     | u32       |                |

### 8.3. Heartbeat (опционально для v1.0.0)

Расширить `send_modem_status()` (он уже шлётся раз в секунду на 5007) полями:
- `uptime_sec` (u32)
- `last_reboot_reason` (u32)
- `fw_version` (3 байта)
- кратко счётчики ошибок radio_stream

### 8.4. Документация

- Обновить `README.md`: убрать упоминания `IS_MASTER/IS_SLAVE`, добавить про
  config storage.
- Обновить `docs/CONTROL_PLANE.md`: добавить group=3 команды.
- Добавить `docs/CONFIG_STORAGE.md` — описание QSPI layout, формата конфига,
  процедуры смены роли.

### 8.5. Тег v1.0.0

После всего — `git tag v1.0.0`.

---

## После v1.0.0 (на потом)

- Удалить legacy 4800-режим, `cmd_buf`, `legacy_path.c` → v1.1.
- Ретрансляция (relay role) — когда появится конкретика.
- Логгер по UDP / ring buffer + flush из main → если понадобится для диагностики
  в поле.
- Runtime LOG_LEVEL через UDP-команду.
- `.clang-format` + прогон по всему репо.
- CI/автотесты (если будет хост-окружение для unit-тестов на пакетный парсер).

---

## Чеклист быстрого доступа

```
[ ] Этап 1 — Логгер
    [ ] log.h создан
    [ ] xil_printf заменены
    [ ] printf убраны
    [ ] hot path = LOG_T

[ ] Этап 2 — Watchdog (твоё)
    [ ] SCU WDT работает
    [ ] wdt_kick в main loop
    [ ] boot reason логируется

[ ] Этап 3 — QSPI storage
    [ ] device_config структура
    [ ] config_load / config_save
    [ ] atomic update (slot A/B)
    [ ] defaults на пустом флэше
    [ ] глобалы читаются из current_config

[ ] Этап 4 — Live re-init
    [ ] IS_MASTER/IS_SLAVE удалены
    [ ] device_reinit() работает
    [ ] init_udp_listeners() вынесен
    [ ] apply_role_specific_init() работает

[ ] Этап 5 — UDP-команды смены конфига
    [ ] 0xA0 group=3 subcmd 0x00..0x30
    [ ] ACK перед re-init
    [ ] config_save после успешного re-init
    [ ] broadcast set-ip

[ ] Этап 6 — Чистка
    [ ] radio_stream.c.txt удалён
    [ ] Закомментированные блоки удалены
    [ ] DEB_FUNC удалён
    [ ] Кракозябры в radio_stream.c починены

[ ] Этап 7 — Разделение main.c
    [ ] ad9361_config.c/h
    [ ] isr.c/h
    [ ] uart_cmd.c/h
    [ ] status_send.c/h
    [ ] video_path.c/h
    [ ] legacy_path.c/h
    [ ] fifo_debug.c/h
    [ ] net_setup.c/h
    [ ] main.c ≤ 300 строк

[ ] Этап 8 — v1.0.0
    [ ] version.h
    [ ] команда get version
    [ ] heartbeat расширен
    [ ] README, CONTROL_PLANE обновлены
    [ ] CONFIG_STORAGE.md
    [ ] git tag v1.0.0
```

---

## Принципы

- **Один этап = один тег.** Чтобы было куда откатываться.
- **Не смешивать чистку с рефакторингом.** Сначала удалить, потом перекомпоновать.
- **Не трогать радио-протокол.** Преамбулы, CRC, размеры блоков, формат PL-обвязки
  — это контракт с PL. Менять только синхронно.
- **Никаких новых фич до v1.0.0.** Heartbeat и version info — это инфраструктура,
  не фичи. Ретрансляция и runtime LOG_LEVEL — после.
- **Тестировать на каждом этапе минимум:** `0x90 0x20` (статистика), радио TX↔RX
  работает, control plane по UDP работает.
