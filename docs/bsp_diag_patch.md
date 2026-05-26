# Врезки в BSP (вне git — переприменять после регена BSP)

BSP (`top_bd_wrapper/.../bsp/.../libsrc/...`) лежит ВНЕ git-репо `src` и
**стирается при регенерации BSP** в Vitis. Здесь — ручные диагностические/фикс
врезки, чтобы их можно было переприменить.

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
