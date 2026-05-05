

```
Ethernet (xemac) ←→ [port1_bridge] ←→ [bridgeif] ←→ [port3_bridge] ←→ [Firewall] ←→ Radio (AXI FIFO)
                                           ↑
                                     bridge_netif
                                    192.168.250.x
                                     UDP:60012
```

## Сегментация пакетов (radio_stream.c)

Ethernet-фреймы перед отправкой в радиоканал оборачиваются в транспортные единицы и нарезаются на блоки по 188 байт:

```
TX: ETH frame → [MAGIC 0xFACE][ID][LEN][DATA][CRC32] → блоки 188 байт → AXI FIFO
RX: AXI FIFO → блоки 188 байт → сборка → проверка CRC32 → ETH frame → bridge
```

Структура блока (188 байт):
- Байт 0: циклический счётчик (обнаружение потерь)
- Байты 1–187: полезная нагрузка

Подробное описание — документ **1.1.5 Сегментация пакетов**.

## Сетевой экран (firewall.c / firewall.h)

Фильтрация исходящего трафика (ETH → Радио) в `vport3_linkoutput`:

| Параметр | Описание |
|----------|----------|
| `ip_mask` | Маска подсети, эталон = IP bridge_netif. `0.0.0.0` = не фильтровать |
| `arp_en` | Пропускать ARP (0x01/0x00) |
| `icmp_en` | Пропускать ICMP |
| `tcp_en` | Пропускать TCP |
| `udp_en` | Пропускать UDP |

Управление по UDP:60012 — команды `0x05` (set), `0x06` (get), `0x07` (set IP broadcast).

По умолчанию при старте всё разрешено.

## Настройка lwIP (opt.h)

> **ВАЖНО:** Изменения нужно внести во **все три** файла `opt.h` в проекте:

```
1. .../export/.../bspinclude/include/lwip/opt.h
2. .../bsp/.../include/lwip/opt.h
3. .../libsrc/lwip211_v1_7/src/lwip-2.1.1/src/include/lwip/opt.h
```

| Параметр | Было | Стало | Зачем |
|----------|------|-------|-------|
| `LWIP_NUM_NETIF_CLIENT_DATA` | 0 | **7** | Поддержка bridgeif (хранение FDB и портов) |
| `IP_SOF_BROADCAST` | 0 | **1** | Разрешение отправки/приёма broadcast на PCB |
| `IP_SOF_BROADCAST_RECV` | 0 | **1** | Фильтрация broadcast на приёме по флагу SOF_BROADCAST |

## Файлы 

| Файл | Описание |
|------|----------|
| `radio_stream.c/h` | Сегментация/десегментация, CRC32 |
| `firewall.c/h` | Сетевой экран |
| `fifo_test.c` | Отладка записи, чтения fifo |

## TODO

- [ ] Приём global broadcast 255.255.255.255 (сейчас работает только subnet broadcast, например 192.168.250.255)


