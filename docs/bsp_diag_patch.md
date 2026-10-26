# Врезки в BSP (вне git — переприменять после регена BSP)

BSP (`top_bd_wrapper/.../bsp/.../libsrc/...`) лежит ВНЕ git-репо `src` и
**стирается при регенерации BSP** в Vitis. Здесь — ручные диагностические/фикс
врезки, чтобы их можно было переприменить.

---

## ФИКС — усечение кадров 1514 байт на GEM-TX (переприменять ОБЯЗАТЕЛЬНО)

Единственная врезка, которая не диагностическая, а рабочая. Без неё ломается
видео от источников, шлющих RTP во всю MTU.

**Файл:** `.../lwip211_v1_7/src/contrib/ports/xilinx/netif/xemacpsif_dma.c`,
функция `sgsend` (~строка 364).

Стоковый код:
```c
		max_fr_size = XEMACPS_MAX_FRAME_SIZE - 18;
		if (q->len > max_fr_size)
			XEmacPs_BdSetLength(txbd, max_fr_size & 0x3FFF);
```

`XEMACPS_MAX_FRAME_SIZE` = `XEMACPS_MTU(1500) + XEMACPS_HDR_SIZE(14) +
XEMACPS_TRL_SIZE(4)` = **1518**, минус 18 = **1500**. А полный кадр Ethernet в
pbuf'е — **1514** байт: 14 байт заголовка плюс 1500 данных, FCS дописывает
железо. То есть любой полноразмерный кадр **молча усекался до 1500**, терялись
последние 14 байт. Не дроп, а именно обрезка: счётчики чистые, ошибок нет,
а данные битые.

Чем это ловилось: видео с камеры, которая пакует RTP во всю MTU (SIYI), не
собиралось вовсе — NAL приходил неполным. Наши скрипты не страдали, потому что
в них `rtph264pay mtu=1212`, и пакеты до предела не доставали.

Замена — вычитать только FCS:
```c
		max_fr_size = XEMACPS_MAX_FRAME_SIZE - XEMACPS_TRL_SIZE;   /* 1514 */
```
(и так же в ветке `ZYNQMP_USE_JUMBO`). После этого обрезка не срабатывает ни
на одном валидном кадре.

**Второй вариант, без правки BSP:** не пускать в GEM-TX кадры длиннее 1500 —
опустить MTU на мостовом порту (`bridge_func.c`, `netif->mtu = 1500`) примерно
до 1486, либо заставить источник паковать мельче. Для своих отправителей
второе и сделано (`rtph264pay mtu=1212` в `video_send_*.sh`), но стороннюю
камеру так не настроишь, поэтому рабочий вариант — первый.

**Применено заново 2026-10-19** после регена BSP от 16.10 (обновление `.xsa`).
Резервная копия стокового файла рядом: `xemacpsif_dma.c.orig`.

---

## DIAG (B), FW r6 — счётчик GEM-ошибок

Цель: понять, гонит ли error/reset-путь GEM (`emacps_error_handler` →
`HandleEmacPsError`) залипание мастера. Читается по UDP `0x90 0x4B`.

**Файл:** `.../lwip211_v1_7/src/contrib/ports/xilinx/netif/xemacpsif_hw.c`

Перед `void emacps_error_handler(...)` добавить:
```c
/* DIAG (B): счётчик входов в обработчик ошибок GEM + последняя ошибка. */
volatile u32 g_gem_err_count     = 0;
volatile u32 g_gem_err_last_word = 0;
volatile u8  g_gem_err_last_dir  = 0;
```

Внутри `emacps_error_handler`, после строк
`rxring = ...; txring = &XEmacPs_GetTxRing(...);` и перед `if (ErrorWord != 0)`:
```c
	g_gem_err_count++;            /* DIAG (B) */
	g_gem_err_last_dir  = Direction;
	g_gem_err_last_word = ErrorWord;
```

App-сторона (в git): `cmd_udp.c` команда `0x90 0x4B` → `[2..5]=err_count
[6..9]=last_ErrorWord [10]=last_Direction`. ErrorWord биты — см. XEMACPS_RXSR_*/
TXSR_* (HRESPNOK, RXOVR, BUFFNA, URUN, BUFEXH...). Direction: 1=RECV, 2=SEND
(XEMACPS_RECV/XEMACPS_SEND).

**РЕЗУЛЬТАТ (3 фриза, 2026-05-27): err_count=0 перед всеми → error-путь НЕВИНОВЕН.**

---

## DIAG (B), FW r8 — section-маркер в process_sent_bds (TX-done ISR)

Локализация фриза в нормальном пути. Гипотеза: спин в TX-done ISR
(`process_sent_bds` while(1)) под тяжёлым GEM-TX. crash_section отражает main loop,
а ISR-спин его не меняет — поэтому ISR сам помечает секцию (save/restore).

Репёрпоз мёртвых section-ID (см. watchdog.h): **12=SPI_RSSI = process_sent_bds**
(также зарезервированы 10=AD9361_TEMP=low_level_output, 7=AD9361_SPI=xemacpsif_input
для итерации 2). XADC(11) НЕ трогать — активен.

**Файл:** `.../netif/xemacpsif_dma.c`, функция `process_sent_bds`:
1. В блок объявлений (после `u32_t index;`):
```c
	extern void wdt_section_mark(int);
	extern unsigned char wdt_section_get(void);
	unsigned char _diag_sec;
```
2. Перед `while (1) {`:
```c
	_diag_sec = wdt_section_get();
	wdt_section_mark(12);   /* SPI_RSSI=process_sent_bds */
```
3. В выходе `if (n_bds == 0) { return; }` — перед `return;`:
```c
		wdt_section_mark(_diag_sec);
```

App-сторона (в git): `watchdog.h/.c` — `wdt_section_get()` + алиасы
WDT_SEC_GEM_INPUT/TXOUT/SENT.

**Проверка:** r8, cap off (0x4A не нужен), 2.5M. Если фриз → `0x40` crash_section =
**SPI_RSSI** → виновен process_sent_bds (фикс: ограничить while(1) — N BD за вызов).
Если crash_section = **LWIP_INPUT** (как раньше) → не process_sent_bds, итерация 2
(маркеры в low_level_output=AD9361_TEMP / xemacpsif_input=AD9361_SPI).

**РЕЗУЛЬТАТ r8 (2 фриза): crash_section=1(LWIP_INPUT), НЕ 12 → process_sent_bds ИСКЛЮЧЁН.**

---

## DIAG (B) r9 — маркеры в нормальном пути (итерация 2)

Локализуем дальше. Маппинг (watchdog.h): 7=AD9361_SPI, 8=VID_SEND, 10=AD9361_TEMP
(все мёртвы при текущем билде; 11=XADC и 12=SPI_RSSI заняты — 11 активен, 12=r8).
Везде save/restore вокруг блока (extern void wdt_section_mark(int);
extern unsigned char wdt_section_get(void); unsigned char _diag_sec;).

1. **`xemacpsif.c` `low_level_output`** → mark **10** (GEM-TX). Декл после
   `xemacpsif_s *xemacpsif = ...(xemac->state);`; `wdt_section_mark(10);` перед
   `SYS_ARCH_PROTECT(lev);`; restore перед `return err;` (метка return_pack_dropped).
2. **`xemacpsif.c` `xemacpsif_input`** → mark **7** (netif->input = ethernet_input/мост).
   Декл после `SYS_ARCH_DECL_PROTECT(lev);`; вокруг `if (netif->input(p, netif)...)`:
   `_diag_sec=wdt_section_get(); wdt_section_mark(7);` до, `wdt_section_mark(_diag_sec);` после.
3. **`xemacpsif_dma.c` `emacps_recv_handler`** → mark **8** (RX-ISR while(1)). Декл после
   `u32_t gigeversion;`; `wdt_section_mark(8);` перед `while(1) {`; restore после
   закрытия while (перед `#if !NO_SYS sys_sem_signal`).

**Проверка:** r9, cap off, 2.5M. crash_section на фризе:
- **10 (AD9361_TEMP)** → low_level_output / GEM-TX;
- **7 (AD9361_SPI)** → netif->input (ethernet_input / мост);
- **8 (VID_SEND)** → RX-ISR emacps_recv_handler (спин в RX while(1));
- **1 (LWIP_INPUT)** остался → в самом xemacpsif_input / low_level_input (тривиально — пересмотр).
