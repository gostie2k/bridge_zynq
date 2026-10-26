#include "modem.h"
#include "lwip/stats.h"
#include "xil_printf.h"
#include <stdlib.h>
#include <string.h>

extern struct ppos_tx tx_chan0;
extern struct ppos_rx rx_chan0;
extern struct ad9361_rf_phy *ad9361_phy;
extern struct ppos_par ppos;

extern  struct udp_pcb *debug_pcb;
extern  struct netif bridge_netif;

struct stat_modem* stat_modem_init()
{
    struct stat_modem* status_modem;
    status_modem = malloc(sizeof(struct stat_modem));
    if (status_modem == NULL) return NULL;
    /* Обнуляем: накопление статистики канала ветвится по is_clr, а из malloc
     * там мусор — при значении, отличном от 0 и 1, не сработала бы ни одна
     * ветка и статистика осталась бы пустой навсегда. is_clr = 1 означает
     * «начать счёт с первой же записи». */
    memset(status_modem, 0, sizeof(struct stat_modem));
    status_modem->stat_rx.is_clr    = 1;
    status_modem->stat_cntrl.is_clr = 1;
    return status_modem;
}


/* ========== lwIP stats  ========== */

/**
 * Fill stats_resp_t with current data from lwIP.
 *
 * Requirements in lwipopts.h:
 *   #define LWIP_STATS      1
 *   #define MEM_STATS       1
 *   #define MEMP_STATS      1
 *   #define LINK_STATS      1
 */
void modem_fill_stats(stats_resp_t *resp)
{
    if (resp == NULL) return;

    resp->head[0] = CMD_PROTO_MAGIC;
    resp->head[1] = CMD_GET_STATS;

#if LWIP_STATS && MEM_STATS
    resp->mem_used = (uint32_t)lwip_stats.mem.used;
    resp->mem_max  = (uint32_t)lwip_stats.mem.max;
    resp->mem_err  = (uint32_t)lwip_stats.mem.err;
#else
    resp->mem_used = resp->mem_max = resp->mem_err = 0;
#endif

#if LWIP_STATS && MEMP_STATS
    const struct stats_mem *pbuf_s = lwip_stats.memp[MEMP_PBUF];
    if (pbuf_s) {
        resp->pbuf_used = (uint32_t)pbuf_s->used;
        resp->pbuf_max  = (uint32_t)pbuf_s->max;
        resp->pbuf_err  = (uint32_t)pbuf_s->err;
    } else {
        resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    }

    const struct stats_mem *pool_s = lwip_stats.memp[MEMP_PBUF_POOL];
    if (pool_s) {
        resp->pool_used = (uint32_t)pool_s->used;
        resp->pool_max  = (uint32_t)pool_s->max;
        resp->pool_err  = (uint32_t)pool_s->err;
    } else {
        resp->pool_used = resp->pool_max = resp->pool_err = 0;
    }
#else
    resp->pbuf_used = resp->pbuf_max = resp->pbuf_err = 0;
    resp->pool_used = resp->pool_max = resp->pool_err = 0;
#endif

#if LWIP_STATS && LINK_STATS
    resp->link_drop = (uint32_t)lwip_stats.link.drop;
#else
    resp->link_drop = 0;
#endif
}

void modem_print_stats(void)
{
    stats_resp_t resp;
    modem_fill_stats(&resp);

    xil_printf("=== lwIP stats ===\r\n");
    xil_printf("  Heap:      used=%u  max=%u  err=%u\r\n",
               resp.mem_used, resp.mem_max, resp.mem_err);
    xil_printf("  PBUF hdr:  used=%u  max=%u  err=%u\r\n",
               resp.pbuf_used, resp.pbuf_max, resp.pbuf_err);
    xil_printf("  PBUF pool: used=%u  max=%u  err=%u\r\n",
               resp.pool_used, resp.pool_max, resp.pool_err);
    xil_printf("  Link drop: %u\r\n", resp.link_drop);
}


/* ========== SPI Engine functions (from video_debug version) ========== */



void spi_engine_ad9361_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_AD9361_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 0;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;



    struct no_os_spi_init_param no_os_spi_init= {0};;
    no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    no_os_spi_init.mode=1;
    no_os_spi_init.device_id = 0;
    no_os_spi_init.chip_select = 0;
    no_os_spi_init.max_speed_hz = 50000000;


    spi_engine_init(spi_eng, &no_os_spi_init);

    return;
}



#if NPU_CELL_NEW

/* ===================================================================
 *  Ячейка распределения питания и управления ЕРВА.468349.020 (А12)
 *
 *  Протокол информационного сопряжения от NN октября 2026 г.
 *  Карта портов сверена со схемой ЕРВА.468349.020 Э3, лист 2 (DD9):
 *
 *    P4, P5     V1, V2          выход  преселектор ячейки фильтрации
 *    P6..P9     1_V_TEMP1..4    вход   перегрев передающей панели N1
 *    P10..P13   1_V_POWER1..4   вход   мощность передающей панели N1
 *    P14        EN1             выход  питание передающей панели N1
 *    P15..P18   2_V_TEMP1..4    вход   перегрев передающей панели N2
 *    P19..P22   2_V_POWER1..4   вход   мощность передающей панели N2
 *    P23        EN2             выход  питание передающей панели N2
 *    P24..P27   EN3..EN6        выход  питание приёмных панелей N1..N4
 *    P28        INT             вход   прерывание моста SPI-I2C (DD11)
 *    P29..P31   RES1..RES3      резерв контрольные точки X4, X3, X2
 *
 *  Вторая микросхема (бывшая pow board) с платы ушла, всё управление
 *  портами на одной MAX7301. Контакт 5 разъёма XP3 теперь CONT_TG
 *  (инверсная тангента), а не второй chip select.
 *
 *  Мост SPI-I2C (DD11) и восемь INA226 за ним протоколом сопряжения не
 *  описаны, но в прошивке реализованы — см. блок NPU_BRIDGE_DEBUG ниже
 *  и docs/SPI_I2C_BRIDGE.md. CS моста приходит с отдельной ноги ПЛИС
 *  AB_GPIO7R (инвертор DA2 отрезан разрывом R18 и не работает).
 * =================================================================== */

#define MAX7301_REG_CONFIG      0x04   /* D0: 1 = normal, 0 = shutdown     */
#define MAX7301_REG_CFG_P7_P4   0x09   /* конфигурация портов, по 2 бита   */
#define MAX7301_REG_CFG_P11_P8  0x0A
#define MAX7301_REG_CFG_P15_P12 0x0B
#define MAX7301_REG_CFG_P19_P16 0x0C
#define MAX7301_REG_CFG_P23_P20 0x0D
#define MAX7301_REG_CFG_P27_P24 0x0E
#define MAX7301_REG_CFG_P31_P28 0x0F

/* Групповая запись восьми портов, начиная с P4: D0 -> P4 (V1), D1 -> P5
 * (V2). Адрес группы — 0x40 + номер первого порта, то есть 0x44.
 * ⚠ В протоколе от 15.10.2026 здесь опечатка: п. 2.2 даёт 16'h4003, а
 * п. 2.2.1 — 12'h400,{2'b00,V2,V1}, то есть адрес 0x40. У MAX7301 портов
 * P0..P3 нет, групповые адреса 0x40..0x43 не определены, и запись
 * туда уходит в никуда. Сам документ это правило и подтверждает:
 * 16'h5700 — группа с P23 (0x40+23), 16'h5F01 — с P31 (0x40+31),
 * 16'h2401 — одиночный P4 (0x20+4). Последствия опечатки: не
 * применялось начальное состояние V1 = V2 = 1 и не работало
 * переключение диапазона из GUI (func_type 19). Правку передать
 * автору протокола. */
#define MAX7301_REG_P4_P11      0x44
#define MAX7301_REG_P14         0x2E   /* одиночный порт:  0x20 + номер    */
#define MAX7301_REG_P23_P30     0x57   /* группа 8 портов, D0 -> P23       */
#define MAX7301_REG_P31         0x5F

#define MAX7301_REG_P6_P13      0x46   /* индикаторы передающей панели N1  */
#define MAX7301_REG_P15_P22     0x4F   /* индикаторы передающей панели N2  */

#define MAX7301_READ            0x80   /* старший бит адреса = чтение      */


/* Адреса одиночных портов питания панелей, индекс — com_dev_type */
static const uint8_t npu_panel_reg[6] = {
    0x2E,   /* TX_PANEL_1 -> P14, EN1 */
    0x37,   /* TX_PANEL_2 -> P23, EN2 */
    0x38,   /* RX_PANEL_1 -> P24, EN3 */
    0x39,   /* RX_PANEL_2 -> P25, EN4 */
    0x3A,   /* RX_PANEL_3 -> P26, EN5 */
    0x3B    /* RX_PANEL_4 -> P27, EN6 */
};


static void max7301_write(struct no_os_spi_desc* spi_eng, uint8_t reg, uint8_t val)
{
    uint8_t tx_buf[2];
    tx_buf[0] = reg;
    tx_buf[1] = val;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    usleep(100);
}


/* Чтение MAX7301 двухкадровое: в первом кадре уходит адрес, содержимое
 * регистра приходит вторым байтом СЛЕДУЮЩЕГО кадра (даташит, раздел
 * Reading Device Registers, шаг 6). Вторым кадром шлём No-Op. */
static uint8_t max7301_read(struct no_os_spi_desc* spi_eng, uint8_t reg)
{
    uint8_t tx_buf[2];

    tx_buf[0] = MAX7301_READ | reg;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    /* Пауза между кадрами. Было 10 мкс — при 25 МГц это 1000 тактов ILA
     * против 64 на сам кадр, и пара «адрес + No-Op» переставала влезать
     * в окно на 1024 отсчёта. MAX7301 требует от CS-high лишь десятки
     * наносекунд, так что 2 мкс с огромным запасом. */
    usleep(2);

    tx_buf[0] = 0x00;                 /* No-Op */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    return tx_buf[1];
}


/* ===================================================================
 *  Запись с обратным чтением — перенесено из отладочного дерева
 *  debug_spi_ppos. Печатает в UART результат каждой записи при
 *  инициализации: если ячейка не отвечает, это видно сразу и по
 *  конкретному регистру, а не по молчащим выходам.
 *
 *  Поставить 0, когда отладка не нужна: тогда wr_v вырождается в
 *  обычную запись, лишних кадров по SPI не идёт.
 * =================================================================== */
#define MAX7301_VERIFY_INIT  1

/* strict = 1 — сверять прочитанное с записанным. Годится только для
 * конфигурационных регистров 0x09..0x0F и 0x04: они хранят то, что в
 * них положили.
 *
 * strict = 0 — прочитать и напечатать без сверки. Так идут все
 * регистры данных портов. Причина: до выхода из shutdown MAX7301
 * удерживает выводы входами, и чтение порта возвращает уровень на
 * ноге, а не защёлку. У групповых регистров к этому добавляется смесь
 * выходов и входов в одном байте. Сверять нечего — важно лишь то, что
 * ответ вообще приходит. */
static int max7301_wr_v(struct no_os_spi_desc* spi_eng,
                        uint8_t reg, uint8_t val,
                        uint8_t strict, const char* what)
{
    max7301_write(spi_eng, reg, val);

#if MAX7301_VERIFY_INIT
    uint8_t rb = max7301_read(spi_eng, reg);

    if (strict) {
        if (rb == val) {
            xil_printf("  [OK]   0x%02X <- 0x%02X            : %s\r\n",
                       reg, val, what);
            return 0;
        }
        xil_printf("  [FAIL] 0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
                   reg, val, rb, what);
        return -1;
    }

    xil_printf("  [WR]   0x%02X <- 0x%02X, read 0x%02X : %s\r\n",
               reg, val, rb, what);
    return 0;
#else

    (void)reg; (void)val; (void)strict; (void)what;
    return 0;
#endif
}


/* ===================================================================
 *  Мост SPI->I2C SC18IS604 (DD11) на плате ячейки — отладочный режим
 * ===================================================================
 *
 *  На шине ячейки две микросхемы: MAX7301 (порты) и SC18IS604 (мост
 *  к восьми измерителям INA226). У каждой свой chip select.
 *
 *  Адресация. В ПЛИС {npu_ant_cs1, npu_ant_cs2} = spi01_cs, то есть
 *  cs[1] — это CS1 (макс), cs[0] — cs2 (мост). Движок опускает ровно
 *  один CS за кадр, поэтому при chip_select = NPU_BRIDGE_CS выбран
 *  только мост, при NPU_BOARD_CS — только макс.
 *
 *  Куда уходит cs2. В ppos_dsp.v он заведён на отдельный порт
 *  AB_GPIO7R: M_AB_GPIO7R = npu_ant_cs2 безусловно, а разрешение
 *  выхода — только в профиле 3 (D_AB_GPIO7R = (TYPE == 4'b0011)).
 *  ⚠ Отсюда жёсткая связь: NPU_GPIO_PROFILE обязан оставаться 3, иначе
 *  линия уходит в Z, CS моста повисает, и мост замолкает без единого
 *  признака почему. На плате цепь CS2 от инвертора DA2 отрезана
 *  разрывом R18, а на пятачок со стороны DD11 заведён провод с
 *  AB_GPIO7R; сам DA2 остался и никуда не работает.
 *
 *  Чтение. MISO моста (DD11.14 через R23) сидит на общей линии
 *  напрямую, а выход макса идёт через буфер DD8 (SN74LVC1G125), у
 *  которого OE висит на CS1. При выбранном мосте CS1 высокий и DD8 в
 *  Z; при выбранном максе молчит мост. Линия всегда у кого-то одного.
 *
 *  Границы кадра. Даташит требует tCSW — не меньше 0,4 мкс высокого CS
 *  между транзакциями. Отдельного ожидания в коде нет и не нужно:
 *  движок перед каждой передачей заново программирует CLK_DIV, LEN и
 *  CONFIG через AXI, и этой паузы хватает с запасом. На cs_delay
 *  полагаться нельзя — в spi_engine.c он усекается до двух разрядов.
 *
 *  Переходный вариант (NPU_BRIDGE_OWN_CS 0) сохранён: там CS моста
 *  получался инверсией CS1, кадр приходилось ограничивать обращением
 *  к максу (max7301_noop), и режим SPI у обеих микросхем обязан был
 *  совпадать. Нужен, только если придётся вернуться на плату без
 *  разрыва R18.
 */
#if NPU_BRIDGE_DEBUG

/* Команды моста (даташит SC18IS604 rev.1.0, раздел 6.5) */
#define BRIDGE_CMD_WR_I2C       0x00   /* запись N байт в устройство I2C  */
#define BRIDGE_CMD_RD_I2C       0x01   /* чтение N байт из устройства     */
#define BRIDGE_CMD_RD_AFTER_WR  0x02   /* запись, повторный старт, чтение */
#define BRIDGE_CMD_RD_BUF       0x06   /* забрать принятое из буфера      */
#define BRIDGE_CMD_WR_REG       0x20   /* запись внутреннего регистра     */
#define BRIDGE_CMD_RD_REG       0x21   /* чтение внутреннего регистра     */
#define BRIDGE_CMD_VERSION      0xFE   /* строка версии в буфер чтения    */

/* Внутренние регистры моста (раздел 6.1) */
#define BRIDGE_REG_IOCONFIG     0x00
#define BRIDGE_REG_IOSTATE      0x01   /* уровни GPIO0..GPIO4             */
#define BRIDGE_REG_I2CCLOCK     0x02   /* частота = 1,875 МГц / N         */
#define BRIDGE_REG_I2CTO        0x03   /* таймаут, бит 0 — разрешение     */
#define BRIDGE_REG_I2CSTAT      0x04
#define BRIDGE_REG_I2CADR       0x05

/* Коды I2CStat (таблица 9) */
#define BRIDGE_STAT_OK          0xF0
#define BRIDGE_STAT_NACK_ADR    0xF1
#define BRIDGE_STAT_NACK_DAT    0xF2
#define BRIDGE_STAT_BUSY        0xF3
#define BRIDGE_STAT_TIMEOUT     0xF8
#define BRIDGE_STAT_BADCOUNT    0xF9

/* Задержка выдачи буфера, в байтах.
 *
 * Мост начинает выдвигать данные не сразу за командой, а через один
 * байт: команду он видит только по восьмому такту, и следующий байт
 * уходит на выборку. Проверено на живой плате — ответ на 0xFE пришёл
 * как FF 53 43 31 38 49 53 36 30 34 20 31 2E 30 2E 33, то есть
 * "SC18IS604 1.0.3" после одного лишнего FF.
 *
 * Держим это числом, а не правкой индексов по месту: если мост другой
 * ревизии поведёт себя иначе, меняется одна строка. Все чтения ниже
 * шлют (данные + BRIDGE_RD_LAG) байт и берут ответ со смещением. */
#define BRIDGE_RD_LAG           1

/* I2CClock = 5 даёт 375 кГц — предел моста по таблице 7 и впятеро
 * быстрее сброса (0x19 = 25, то есть 75 кГц). INA226 держит 400 кГц. */
#define BRIDGE_I2C_CLK_DIV      5
/* Таймаут шины: биты 7:1 — значение, бит 0 — разрешение. По сбросу
 * выключен, а без него зависшее устройство вешает мост насовсем. */
#define BRIDGE_I2C_TIMEOUT      0x7F
/* Предел опроса I2CStat. Транзакция read-after-write на 375 кГц идёт
 * около 130 мкс, пауза между опросами 50 мкс — тридцати попыток хватает
 * с запасом. Важно, что цикл ограничен: безлимитное ожидание на этой
 * шине уже стоило нам перезагрузок. */
#define BRIDGE_POLL_MAX         30

/* Адреса INA226 подписаны на схеме у каждой микросхемы и идут подряд */
#define INA226_ADR_FIRST        0x40
#define INA226_ADR_LAST         0x47

#define INA226_REG_SHUNT_V      0x01   /* знаковый, 2,5 мкВ на разряд     */
#define INA226_REG_BUS_V        0x02   /* 1,25 мВ на разряд               */
#define INA226_REG_DIE_ID       0xFF   /* должен читаться как 0x2260      */

static struct no_os_spi_desc* spi_eng_bridge  = NULL;
#if !NPU_BRIDGE_OWN_CS
/* Нужен только ради фронтов на CS1, ограничивающих кадр моста. */
static struct no_os_spi_desc* spi_eng_npu_ref = NULL;
#endif


#if !NPU_BRIDGE_OWN_CS
/* No-Op максу. Нужен не ради самого макса, а ради фронта на CS1:
 * именно он поднимает CS2 и тем ограничивает кадр моста. */
static void max7301_noop(struct no_os_spi_desc* spi_eng)
{
    uint8_t tx_buf[2];
    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
}
#endif  /* !NPU_BRIDGE_OWN_CS */


/* Обмен с мостом в собственном кадре. Буфер полнодуплексный: что
 * ушло по MOSI, тем же местом возвращается принятое по MISO. */
static void bridge_xfer(uint8_t* buf, uint16_t len)
{
    if (!spi_eng_bridge)
        return;

#if NPU_BRIDGE_OWN_CS
    /* У моста свой CS — кадр ограничивает сам движок (CS_HIGH, CS_LOW,
     * обмен, CS_HIGH), обёртки не нужны. */
    spi_engine_write_and_read(spi_eng_bridge, buf, len);
#else
    if (!spi_eng_npu_ref)
        return;

    max7301_noop(spi_eng_npu_ref);                      /* кадр открыт  */
    spi_engine_write_and_read(spi_eng_bridge, buf, len);
    max7301_noop(spi_eng_npu_ref);                      /* кадр закрыт  */
#endif
}


/* Чтение внутреннего регистра: 0x21, номер регистра, холостой байт.
 * Ответ мост кладёт в третий байт кадра. Если окажется, что он
 * съезжает на байт, это сразу видно по строке версии — она известна
 * заранее, ради того команда 0xFE в мосте и существует. */
static uint8_t bridge_read_reg(uint8_t reg)
{
    uint8_t buf[3 + BRIDGE_RD_LAG];
    int i;

    for (i = 0; i < 3 + BRIDGE_RD_LAG; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_REG;
    buf[1] = reg;
    bridge_xfer(buf, 3 + BRIDGE_RD_LAG);
    return buf[2 + BRIDGE_RD_LAG];
}


static void bridge_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[3];
    buf[0] = BRIDGE_CMD_WR_REG;
    buf[1] = reg;
    buf[2] = val;
    bridge_xfer(buf, 3);
}


/* Ожидание конца транзакции на I2C. Возвращает код I2CStat; если мост
 * так и не вышел из BUSY, вернётся BRIDGE_STAT_BUSY. Чтение I2CStat
 * заодно снимает INT моста. */
static uint8_t bridge_wait_done(void)
{
    uint8_t st = BRIDGE_STAT_BUSY;

    for (int i = 0; i < BRIDGE_POLL_MAX; ++i) {
        st = bridge_read_reg(BRIDGE_REG_I2CSTAT);
        if (st != BRIDGE_STAT_BUSY)
            return st;
        usleep(50);
    }
    return st;
}


/* Чтение регистра INA226: указатель регистра, повторный старт, два
 * байта. У моста это одна команда 0x02.
 *
 * ⚠ Буфер чтения у моста один, и даташит прямо предупреждает: любая
 * следующая команда чтения затрёт его до того, как данные заберут.
 * Поэтому строго по одной транзакции за раз — команда, ожидание,
 * выемка буфера. Складывать чтения в очередь нельзя.
 *
 * Возврат: 0 — успех, иначе код I2CStat. */
static int ina226_read(uint8_t i2c_adr, uint8_t reg, uint16_t* val)
{
    uint8_t buf[6];
    uint8_t rb[3 + BRIDGE_RD_LAG];
    uint8_t st;
    int i;

    buf[0] = BRIDGE_CMD_RD_AFTER_WR;
    buf[1] = 1;                                 /* писать: 1 байт        */
    buf[2] = 2;                                 /* читать: 2 байта       */
    buf[3] = (uint8_t)(i2c_adr << 1);           /* адрес + признак записи */
    buf[4] = reg;                               /* указатель регистра    */
    buf[5] = (uint8_t)((i2c_adr << 1) | 1);     /* адрес + признак чтения */
    bridge_xfer(buf, 6);

    st = bridge_wait_done();
    if (st != BRIDGE_STAT_OK)
        return (int)st;

    for (i = 0; i < 3 + BRIDGE_RD_LAG; ++i)
        rb[i] = 0x00;
    rb[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(rb, 3 + BRIDGE_RD_LAG);

    *val = (uint16_t)(((uint16_t)rb[1 + BRIDGE_RD_LAG] << 8)
                      | rb[2 + BRIDGE_RD_LAG]);          /* старшим вперёд */
    return 0;
}


static const char* bridge_stat_name(uint8_t st)
{
    switch (st) {
    case BRIDGE_STAT_OK:       return "успешно";
    case BRIDGE_STAT_NACK_ADR: return "адрес не подтверждён";
    case BRIDGE_STAT_NACK_DAT: return "байт не подтверждён";
    case BRIDGE_STAT_BUSY:     return "занят, ожидание истекло";
    case BRIDGE_STAT_TIMEOUT:  return "таймаут I2C";
    case BRIDGE_STAT_BADCOUNT: return "неверное число байт";
    default:                   return "неизвестный код";
    }
}


/* Создание дескриптора моста. Вызывается рядом с дескриптором макса и
 * ДО первых записей в него: spi_engine_init сбрасывает ядро движка
 * (SPI_ENGINE_REG_RESET), и делать это посреди настройки MAX7301 было
 * бы некстати. Сами параметры кадра — делитель, длина слова, CPOL,
 * CPHA, sdo_idle — движок переписывает из дескриптора на КАЖДЫЙ обмен
 * (spi_engine_compile_message), поэтому два дескриптора на одном ядре
 * сосуществуют без ручного переключения. */
static void bridge_desc_init(struct no_os_spi_desc* spi_eng_max)
{
    struct spi_engine_init_param spi_engine_init_par;
    struct no_os_spi_init_param  no_os_spi_init = {0};

#if NPU_BRIDGE_OWN_CS
    (void)spi_eng_max;
#else
    spi_eng_npu_ref = spi_eng_max;
#endif

    spi_engine_init_par.spi_engine_baseaddr = SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type           = SPI_ENGINE;
    spi_engine_init_par.data_width     = 8;
    spi_engine_init_par.cs_delay       = 0;
    spi_engine_init_par.ref_clk_hz     = 100000000;
    spi_engine_init_par.sdo_idle_state = 0;

    no_os_spi_init.extra        = &spi_engine_init_par;
    no_os_spi_init.mode         = NO_OS_SPI_MODE_3;  /* у моста другого нет */
    no_os_spi_init.device_id    = 1;
    /* cs2, индекс 0 — см. NPU_BRIDGE_CS в platform_config.h. Значение
     * одно и то же и при инверсии CS1, и при собственном CS моста. */
    no_os_spi_init.chip_select  = NPU_BRIDGE_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(&spi_eng_bridge, &no_os_spi_init);
    /* Потолок моста по даташиту 1,2 Мбит/с — выше 1 МГц не поднимать. */
    spi_engine_set_speed(spi_eng_bridge, 1000000);
}


/* Разбор моста при старте. Печатает всё в UART, ничего не запоминает:
 * задача — понять, отвечает ли мост и видит ли он измерители. */
static void bridge_debug(void)
{
    uint8_t buf[17];
    int i;

    xil_printf("\r\n=== Мост SPI->I2C SC18IS604 (DD11) ===\r\n");

    /* 0xFE — диагностическая команда самого NXP: кладёт в буфер чтения
     * строку вида "SC18IS604 1.0.0". Шина I2C при этом не трогается,
     * так что это чистая проверка параметров SPI. Ответ известен
     * заранее, поэтому по нему же видно и сдвиг выдачи буфера, если он
     * вдруг есть. */
    buf[0] = BRIDGE_CMD_VERSION;
    bridge_xfer(buf, 1);

    for (i = 0; i < 17; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(buf, 17);

    xil_printf("  версия, hex :");
    for (i = 1 + BRIDGE_RD_LAG; i < 17; ++i)
        xil_printf(" %02X", buf[i]);
    xil_printf("\r\n  версия, txt : ");
    for (i = 1 + BRIDGE_RD_LAG; i < 17 && buf[i] >= 0x20 && buf[i] < 0x7F; ++i)
        xil_printf("%c", buf[i]);
    xil_printf("\r\n  ожидалось   : 53 43 31 38 49 53 36 30 34 ... \"SC18IS604 ...\"\r\n");

    /* Настройка шины. Читаем обратно: это ещё и проверка того, что
     * команды 0x20 и 0x21 ходят в обе стороны. */
    bridge_write_reg(BRIDGE_REG_I2CCLOCK, BRIDGE_I2C_CLK_DIV);
    bridge_write_reg(BRIDGE_REG_I2CTO,    BRIDGE_I2C_TIMEOUT);

    xil_printf("  I2CClock <- 0x%02X, прочитано 0x%02X   (375 кГц)\r\n",
               BRIDGE_I2C_CLK_DIV, bridge_read_reg(BRIDGE_REG_I2CCLOCK));
    xil_printf("  I2CTO    <- 0x%02X, прочитано 0x%02X   (таймаут шины включён)\r\n",
               BRIDGE_I2C_TIMEOUT, bridge_read_reg(BRIDGE_REG_I2CTO));
    xil_printf("  IOSTATE   = 0x%02X                   (GPIO0..GPIO4 — ALERT от INA226)\r\n",
               bridge_read_reg(BRIDGE_REG_IOSTATE));

    /* Перебор восьми измерителей. Die ID проверяет сразу всю цепочку:
     * SPI до моста, I2C от моста и сам прибор. */
    for (uint8_t a = INA226_ADR_FIRST; a <= INA226_ADR_LAST; ++a) {
        uint16_t id = 0, bus = 0;
        uint16_t sh = 0;
        int rc;

        rc = ina226_read(a, INA226_REG_DIE_ID, &id);
        if (rc != 0) {
            xil_printf("  INA226 0x%02X : нет ответа, I2CStat 0x%02X — %s\r\n",
                       a, (uint8_t)rc, bridge_stat_name((uint8_t)rc));
            continue;
        }

        ina226_read(a, INA226_REG_BUS_V,   &bus);
        ina226_read(a, INA226_REG_SHUNT_V, &sh);

        /* Шина: 1,25 мВ на разряд, то есть отсчёт * 5 / 4.
         * Шунт: 2,5 мкВ на разряд; при 0,2 Ом это 12,5 мкА, то есть
         * ток в мА = отсчёт / 80. Знаковый.
         * ⚠ Предел INA226 по шунту ±81,92 мВ, при 0,2 Ом это всего
         * ±410 мА. Если подрешётки едят больше, шунт выбран неверно и
         * показания упрутся в край. */
        xil_printf("  INA226 0x%02X : ID 0x%04X, шина %d мВ, ток %d мА\r\n",
                   a, id,
                   (int)(((int)bus * 5) / 4),
                   (int)((int)(int16_t)sh / 80));
    }

    xil_printf("=== мост: конец разбора ===\r\n\r\n");
}


/* Опрос моста по запросу с экрана (UDP 0x90 0x19 -> func_type 22).
 *
 * Один кадр 0xFE плюс выборка буфера — девять байт туда, девять
 * обратно, меньше двухсот микросекунд. Печати нет: результат кладётся
 * в кэш и уезжает в ответе на следующий запрос. Так кнопку можно
 * давить сколько угодно часто, не забивая UART.
 *
 * Для осциллографа важно, что каждый вызов опускает CS моста ровно
 * трижды (0xFE, выборка буфера, чтение I2CClock) — пачка хорошо видна
 * и однозначно отличается от обращения к максу по CS1. */
struct bridge_probe bridge_probe_last;

void spi_engine_bridge_probe(void)
{
    uint8_t buf[10];
    int i;

    for (i = 0; i < 10; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_VERSION;
    bridge_xfer(buf, 1);

    for (i = 0; i < 10; ++i)
        buf[i] = 0x00;
    buf[0] = BRIDGE_CMD_RD_BUF;
    bridge_xfer(buf, 9 + BRIDGE_RD_LAG);

    for (i = 0; i < 8; ++i)
        bridge_probe_last.ver[i] = buf[i + 1 + BRIDGE_RD_LAG];

    bridge_probe_last.i2cclk = bridge_read_reg(BRIDGE_REG_I2CCLOCK);
    bridge_probe_last.cnt++;
}


/* Обход восьми измерителей по запросу с экрана (UDP 0x90 0x1A ->
 * func_type 23). Печати нет: результат кладётся в кэш, экран забирает
 * его следующим запросом.
 *
 * Die ID читается первым и служит пропуском. Не ответил — остальные два
 * чтения пропускаем: это экономит по две транзакции на каждом
 * отсутствующем приборе и, главное, не даёт принять нули за показания.
 *
 * Пересчёт в вольты и амперы здесь НЕ делается. Наверх уходят сырые
 * регистры, а цена разряда — дело того, кто их показывает: шунт зашит
 * в делитель, и держать это знание в одном месте (на экране) лучше,
 * чем в двух. */
struct npu_power npu_pwr;

void spi_engine_bridge_sweep(void)
{
    uint8_t a;
    int i;

    for (a = INA226_ADR_FIRST, i = 0; a <= INA226_ADR_LAST; ++a, ++i) {
        uint16_t id = 0, bus = 0, sh = 0;
        int rc;

        npu_pwr.bus[i]   = 0;
        npu_pwr.shunt[i] = 0;

        rc = ina226_read(a, INA226_REG_DIE_ID, &id);
        if (rc != 0) {
            npu_pwr.stat[i] = (uint8_t)rc;
            continue;
        }
        if (id != 0x2260) {
            npu_pwr.stat[i] = 0xFE;
            continue;
        }

        ina226_read(a, INA226_REG_BUS_V,   &bus);
        ina226_read(a, INA226_REG_SHUNT_V, &sh);

        npu_pwr.stat[i]  = 0x00;
        npu_pwr.bus[i]   = bus;
        npu_pwr.shunt[i] = (int16_t)sh;
    }

    npu_pwr.cnt++;
}
#endif  /* NPU_BRIDGE_DEBUG */



void spi_engine_npu_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    /* 0, как было у платы на CS1 в отладочном дереве debug_spi_ppos.
     * Задаёт уровень MOSI в паузах между кадрами; MAX7301 на него
     * смотреть не должен, но это единственное место, где рабочая
     * апрельская конфигурация расходилась с нашей. */
    spi_engine_init_par.sdo_idle_state = 0;

    struct no_os_spi_init_param no_os_spi_init = {0};
    no_os_spi_init.extra = &spi_engine_init_par;
#if NPU_SPI_MODE3
    /* Mode 3 на всей шине: мост SC18IS604 другого не умеет, а MAX7301
     * работает в обоих. Подробности — в platform_config.h. */
    no_os_spi_init.mode = NO_OS_SPI_MODE_3;
#else
    no_os_spi_init.mode = NO_OS_SPI_MODE_0;
#endif
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select = NPU_BOARD_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    /* 1 МГц. Движок делит опорные 100 МГц целочисленно:
     *   clk_div = ref_clk_hz / (2 * speed_hz) - 1
     *   f_sclk  = ref_clk_hz / ((clk_div + 1) * 2)
     * здесь clk_div = 49 и сетка попадает ровно в 1 МГц.
     * Было 25 МГц (clk_div = 1) — предел MAX7301 по даташиту 26 МГц,
     * так что запаса не оставалось вовсе; вернули 1 МГц. Выше 25 МГц
     * ставить нельзя: 26 МГц дают clk_div = 0, то есть 50 МГц. */
    spi_engine_set_speed(*spi_eng, 1000000);

#if NPU_BRIDGE_DEBUG
    /* Дескриптор моста — здесь, пока MAX7301 ещё не настраивали:
     * spi_engine_init сбрасывает ядро движка. */
    bridge_desc_init(*spi_eng);
#endif

    int errs = 0;

    xil_printf("\r\n=== MAX7301 init: ячейка НПУ (один CS) ===\r\n");

    /* MAX7301AAX+ (DD9). Конфигурация портов, по два бита на порт:
     * 00 — не использовать, 01 — выход, 10 — вход, 11 — вход с
     * подтяжкой к лог. 1. */
    max7301_write(*spi_eng, 0x00, 0x00);                        /* No-Op */

    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P7_P4,   0xF5, 1, "P7,P6 вход; P5,P4 выход (V2,V1)") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P11_P8,  0xFF, 1, "P11..P8  вход с подтяжкой") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P15_P12, 0xDF, 1, "P14 выход (EN1), прочие вход") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P19_P16, 0xFF, 1, "P19..P16 вход с подтяжкой") != 0);
    /* В протоколе эта команда записана как 0x0B7F — опечатка: регистр
     * 0x0B задан выше, конфигурация P23..P20 лежит в 0x0D. */
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P23_P20, 0x7F, 1, "P23 выход (EN2), P22..P20 вход") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P27_P24, 0x55, 1, "P27..P24 выход (EN6..EN3)") != 0);
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CFG_P31_P28, 0x5F, 1, "P31,P30 выход; P29,P28 вход") != 0);

    /* Начальные состояния выходов. Пишутся до выхода из shutdown: в
     * shutdown порты удерживаются входами, значения применяются разом
     * при включении, так что питание панелей не моргает. По этой же
     * причине все четыре записи идут без сверки — см. max7301_wr_v. */
    max7301_wr_v(*spi_eng, MAX7301_REG_P4_P11,  0x03, 0, "V2,V1 = 1,1 — без фильтра");
    max7301_wr_v(*spi_eng, MAX7301_REG_P14,     0x00, 0, "EN1 = 0");
    max7301_wr_v(*spi_eng, MAX7301_REG_P23_P30, 0x00, 0, "EN2..EN6 = 0, RES2 = 0");
    max7301_wr_v(*spi_eng, MAX7301_REG_P31,     0x01, 0, "RES3 = 1 (по протоколу)");

    /* Решающая строка: если 0x04 читается обратно как 0x01, ячейка
     * отвечает и вышла из shutdown. Если нет — дальше смотреть нечего,
     * вопрос в CS, питании или обвязке. */
    errs += (max7301_wr_v(*spi_eng, MAX7301_REG_CONFIG, 0x01, 1, "выход из shutdown") != 0);

#if MAX7301_VERIFY_INIT
    if (errs == 0)
        xil_printf("=== ячейка НПУ: OK, ошибок сверки нет ===\r\n\r\n");
    else
        xil_printf("=== ячейка НПУ: ОШИБОК СВЕРКИ %d ===\r\n\r\n", errs);
#else
    (void)errs;
#endif

#if NPU_BRIDGE_DEBUG
    bridge_debug();
#endif

    return;
}


void spi_engine_npu_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{
    /* Разряды обоих байт (протокол, таблица 3): D0..D3 — перегрев
     * каналов 1..4, D4..D7 — мощность каналов 1..4. Порядок совпадает
     * с порядком полей в struct stat_npu, перестановка не нужна. */
    status_npu->tx_panel_1.word = max7301_read(spi_eng, MAX7301_REG_P6_P13);
    status_npu->tx_panel_2.word = max7301_read(spi_eng, MAX7301_REG_P15_P22);
}


void spi_engine_npu_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{
    if ((unsigned int)com_dev > (unsigned int)RX_PANEL_4)
        return;

    max7301_write(spi_eng, npu_panel_reg[com_dev], (ena == DISABLED) ? 0x00 : 0x01);
}


void spi_engine_npu_set_f_diap(struct no_os_spi_desc*  spi_eng, pow_cntrl_type typ)
{
    /* Регистр 0x44 — групповая запись портов с P4: D0 -> P4 (V1),
     * D1 -> P5 (V2). Коды pow_cntrl_type совпадают с таблицей 1
     * протокола: 0 — 4000-4200, 1 — 3700-3980, 2 — 3400-3600,
     * 3 — без фильтра. Старшие разряды приходятся на P6..P11 — они
     * настроены входами, запись в них игнорируется. Про адрес 0x40 из
     * протокола — см. комментарий к MAX7301_REG_P4_P11. */
    max7301_write(spi_eng, MAX7301_REG_P4_P11, (uint8_t)(typ & 0x03));
}


/* Одиночные порты преселектора: V1 на P4, V2 на P5. Пишутся групповым
 * регистром 0x44, а читаются по одному — см. комментарий к struct
 * npu_outputs в modem.h. */
#define MAX7301_REG_P4          0x24
#define MAX7301_REG_P5          0x25


void spi_engine_npu_get_outputs(struct no_os_spi_desc*  spi_eng, struct npu_outputs* out)
{
    out->ena     = 0;
    out->f_diap  = 0;
    out->reserve = 0;

    /* Сначала регистр конфигурации: если ячейка не отвечает, остальные
     * девять чтений всё равно ничего не дадут, а время займут. */
    out->cfg = max7301_read(spi_eng, MAX7301_REG_CONFIG);
    if (!(out->cfg & 0x01))
        return;

    for (uint8_t i = 0; i < 6; ++i) {
        if (max7301_read(spi_eng, npu_panel_reg[i]) & 0x01)
            out->ena |= (uint8_t)(1u << i);
    }

    /* Код диапазона собирается обратно как {V2,V1}: ровно то, что
     * set_f_diap кладёт в порты, так что прочитанное значение сравнимо
     * с pow_cntrl_type напрямую. */
    uint8_t v1 = max7301_read(spi_eng, MAX7301_REG_P4) & 0x01;
    uint8_t v2 = max7301_read(spi_eng, MAX7301_REG_P5) & 0x01;
    out->f_diap = (uint8_t)((v2 << 1) | v1);
}

#else   /* NPU_CELL_NEW */

/* ===================================================================
 *  ПРЕЖНЯЯ ячейка НПУ: одна MAX7301 на единственном chip select
 *  NPU_BOARD_CS, привязанном к плате.
 *
 *  История: по КД было две платы с MAX7301 и два chip select — com
 *  board (питание подрешёток, индикаторы) и pow board (преселектор,
 *  групповая запись 0x40). От pow board пришлось отказаться, и выбор
 *  преселектора перенесли на com board, на свободные порты P26 и P28
 *  (одиночные регистры 0x3A и 0x3C). Здесь — именно эта, вторая
 *  версия: инициализация снятой микросхемы и запись 0x40 убраны, они
 *  уходили в никуда.
 *
 *  Прочее сохранено как есть, вплоть до однокадрового чтения статуса:
 *  это заведомо рабочая ветка под прежнее железо. Разбор расхождений
 *  с даташитом — в ветке NPU_CELL_NEW. Вариант по КД с двумя
 *  микросхемами лежит в git: `git show b8fa035:modem.c`.
 * =================================================================== */

void spi_engine_npu_init(struct no_os_spi_desc**  spi_eng)
{
    struct spi_engine_init_param spi_engine_init_par;
    spi_engine_init_par.spi_engine_baseaddr =  SPI_ENGINE_NPU_BASEADDR;
    spi_engine_init_par.type = SPI_ENGINE;
    spi_engine_init_par.data_width = 8;
    spi_engine_init_par.cs_delay = 100;
    spi_engine_init_par.ref_clk_hz = 100000000;
    spi_engine_init_par.sdo_idle_state = 1;


    struct no_os_spi_init_param no_os_spi_init={0};
    no_os_spi_init.extra = &spi_engine_init_par;
    ////no_os_spi_init.parent = &spi_eng;
    no_os_spi_init.mode=NO_OS_SPI_MODE_0;
    no_os_spi_init.device_id = 1;
    no_os_spi_init.chip_select =  NPU_BOARD_CS;
    no_os_spi_init.max_speed_hz = 1000000;

    spi_engine_init(spi_eng, &no_os_spi_init);
    /* 1 МГц, clk_div = 49. Сетка делителя целочисленная, см. комментарий
     * в ветке новой ячейки. 26 МГц, стоявшие здесь изначально, давали
     * clk_div = 0, то есть 50 МГц — вдвое выше предела MAX7301. */
    spi_engine_set_speed(*spi_eng, 1000000);
    //MAX7301AAX+
    uint8_t tx_buf[2];

    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    //00 -not use, 01 - output, 10- Schmitt logic input, 11 - Schmitt logic input with pullup
    tx_buf[0] = 0x09; // ����� �������� ������������ ������ P[7:4]
    tx_buf[1] = 0xff; // ������: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0A; // ����� �������� ������������ ������ P[11:8]
    tx_buf[1] = 0xff; // ������: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0B; // ����� �������� ������������ ������ P[15:12]
    tx_buf[1] = 0xfd; // ������: 11 11 11 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0C; // ����� �������� ������������ ������ P[19:16]
    tx_buf[1] = 0xff; // ������: 11 11 11 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0D; // ����� �������� ������������ ������ P[23:20]
    tx_buf[1] = 0x57; // ������: 01 01 01 11
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0E; // ����� �������� ������������ ������ P[27:24]
    tx_buf[1] = 0xD5; // ������: 11 01 01 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x0F; // ����� �������� ������������ ������ P[27:24]
    tx_buf[1] = 0x5D; // ������: 01 01 11 01
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    tx_buf[0] = 0x2c; // ������ ������ P�12]
    tx_buf[1] = 0x00; // ������:
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);

    //tx_buf[0] = 0x55; // ������ ������ P[28:21]
    //tx_buf[1] = 0x00; // ������:
    //spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    //usleep(100);


    tx_buf[0] = 0x04; // ����� �������� ������������
    tx_buf[1] = 0x01; // ������: Shutdown Control = Normal Operation
    spi_engine_write_and_read(*spi_eng,(uint8_t *)&tx_buf, 2);
    usleep(100);




    return;


}



/* Двухкадровое чтение регистра, как требует даташит (раздел Reading
 * Device Registers, шаг 6). MAX7301 отдаёт содержимое регистра не в том
 * кадре, в котором пришёл адрес, а вторым байтом СЛЕДУЮЩЕГО кадра:
 * внутри микросхемы адрес и данные разнесены по разным кадрам. Обмен
 * поэтому всегда парный — кадр с адресом (старший бит = чтение) и кадр
 * No-Op 0x0000, который и выталкивает данные на MISO.
 *
 * Однокадровый вариант, стоявший тут раньше, снимал MISO того же кадра,
 * а там едет эхо ПРОШЛОЙ посылки, а не регистр. Именно это кормило
 * индикаторы подрешёток и «Аварию». */
static uint8_t max7301_read(struct no_os_spi_desc* spi_eng, uint8_t reg)
{
    uint8_t tx_buf[2];

    tx_buf[0] = (uint8_t)(0x80 | reg);   /* кадр 1: адрес, бит 15 = чтение */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
    usleep(2);   /* см. комментарий в ветке новой ячейки */

    tx_buf[0] = 0x00;                    /* кадр 2: No-Op, выталкивает данные */
    tx_buf[1] = 0x00;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    return tx_buf[1];
}


/* Индикаторы перегрева и мощности двух передающих подрешёток.
 * Групповые регистры чтения: 0x44 — восемь портов начиная с P4
 * (P4..P11), 0x4D — начиная с P13 (P13..P20). Старший бит адреса
 * ставит max7301_read, поэтому здесь адреса без 0x80: раньше тут
 * стояли 0xC4 и 0xCD, это те же регистры.
 *
 * Разряды байта: D0..D3 — перегрев каналов 1..4, D4..D7 — мощность
 * каналов 1..4; порядок совпадает с полями struct stat_npu. */
void spi_engine_npu_get_status(struct no_os_spi_desc*  spi_eng, struct stat_npu* status_npu)
{
    status_npu->tx_panel_1.word = max7301_read(spi_eng, 0x44);
    status_npu->tx_panel_2.word = max7301_read(spi_eng, 0x4D);
}


 void spi_engine_npu_set_ena(struct no_os_spi_desc*  spi_eng, com_dev_type com_dev,  ena_type ena)
{

   uint8_t tx_buf[2];
   if ( com_dev ==TX_PANEL_1 ) // ������� ������ ���������� �1
   {
		tx_buf[0] = 0x2C;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==TX_PANEL_2 ) // ������� ������ ���������� �2
   {
		tx_buf[0] = 0x35;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_1 ) // ������� ������ �������� �1
   {
		tx_buf[0] = 0x36;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_2 ) // ������� ������ �������� �2
   {
		tx_buf[0] = 0x37;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_3 ) // ������� ������ �������� �3
   {
		tx_buf[0] = 0x38;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

   if ( com_dev ==RX_PANEL_4 ) // ������� ������ �������� �4
   {
		tx_buf[0] = 0x39;
		tx_buf[1] = ena;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
   }

}




/* Преселектор: побитовая запись одиночных портов P26 (0x3A) и
 * P28 (0x3C) той же микросхемы. Оба настроены выходами в
 * spi_engine_npu_init (0x0E = 0xD5 и 0x0F = 0x5D). */
void spi_engine_npu_set_f_diap(struct no_os_spi_desc*  spi_eng, pow_cntrl_type typ)
{
    uint8_t tx_buf[2];

    uint8_t typ_1 = (typ & 0x02) >> 1;
    uint8_t typ_0 =  typ & 0x01;

    tx_buf[0] = 0x3A;
    tx_buf[1] = typ_1;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);

    tx_buf[0] = 0x3C;
    tx_buf[1] = typ_0;
    spi_engine_write_and_read(spi_eng, (uint8_t *)&tx_buf, 2);
}




/* Регистры одиночных портов — те же, в которые пишут set_ena и
 * set_f_diap этой ветки. Порядок — com_dev_type. */
static const uint8_t npu_panel_reg_old[6] = {
    0x2C,   /* TX_PANEL_1 -> P12 */
    0x35,   /* TX_PANEL_2 -> P21 */
    0x36,   /* RX_PANEL_1 -> P22 */
    0x37,   /* RX_PANEL_2 -> P23 */
    0x38,   /* RX_PANEL_3 -> P24 */
    0x39    /* RX_PANEL_4 -> P25 */
};


void spi_engine_npu_get_outputs(struct no_os_spi_desc*  spi_eng, struct npu_outputs* out)
{
    out->ena     = 0;
    out->f_diap  = 0;
    out->reserve = 0;

    /* Сначала регистр конфигурации: если ячейка не отвечает, остальные
     * восемь чтений всё равно ничего не дадут, а время займут. */
    out->cfg = max7301_read(spi_eng, 0x04);
    if (!(out->cfg & 0x01))
        return;

    for (uint8_t i = 0; i < 6; ++i) {
        if (max7301_read(spi_eng, npu_panel_reg_old[i]) & 0x01)
            out->ena |= (uint8_t)(1u << i);
    }

    /* Преселектор этой ячейки разведён на два одиночных порта: P26 —
     * старший бит кода, P28 — младший (см. set_f_diap выше). */
    uint8_t p26 = max7301_read(spi_eng, 0x3A) & 0x01;
    uint8_t p28 = max7301_read(spi_eng, 0x3C) & 0x01;
    out->f_diap = (uint8_t)((p26 << 1) | p28);
}


#endif  /* NPU_CELL_NEW */








void ppos_set_tangenta(ena_type ena)
{
    ppos.creg2.inside.Tang = ena;
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);
}

/* Вторая тангента — CONT_TANG, отдельная линия на другую плату
 * (creg2.NPU_CTX_nCRX, бит 22). От первой не зависит: обе можно держать
 * в разных состояниях. Парный бит NPU_CTX_nCRX_avt отдаёт линию
 * автомату ПЛИС, тогда это значение не смотрится. */
void ppos_set_cont_tang(ena_type ena)
{
    ppos.creg2.inside.NPU_CTX_nCRX = (ena == ENABLED) ? 1 : 0;
    AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);
}












 /*

 void spi_engine_npu_set_sd_on(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x04;
		tx_buf[1] =  0x00;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
}

 void spi_engine_npu_set_sd_off(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x04;
		tx_buf[1] =  0x01;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);
}

 void spi_engine_npu_set_sd_status(struct no_os_spi_desc*  spi_eng)
{
        uint8_t tx_buf[2];
		tx_buf[0] =  0x84;
		tx_buf[1] =  0x00;
		spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

		//tx_buf[0] =  0x00;
		//tx_buf[1] =  0x00;
		//spi_engine_write_and_read(spi_eng,(uint8_t *)&tx_buf, 2);

		xil_printf("npu_set_sd_status = %02X \n\r ", tx_buf[1]);
}




   Функции снятой второй микросхемы (pow board) удалены вместе с ней:
   преселектор и питание УФ теперь на той же MAX7301.
*/




 void modem_init()
 {




	 /* Блок под IS_ANTSDR выставлял Rx1B/Tx1B/Rx2B/Tx2B — в ПЛИС этих
	  * полей больше нет, да и затирались они следующей же строкой. */

	 ppos.creg2.word = 0;

	 /* Профиль распиновки шести линий AB_GPIO1R..6R внутри ppos_dsp:
	  *   0 — вручную из creg2.GPIOnR/GPIOnR_DIR;
	  *   1 — НПУ вер.1;
	  *   2 — борт, плата управляется от модема по GPIO;
	  *   3 — НПУ вер.2 с платой питания от вер.1;
	  *   4 — НПУ вер.2 (распиновка по КД: 1R SCLK, 2R MOSI, 3R MISO,
	  *       4R CS1, 5R CONT_TG, 6R tang).
	  * ⚠ Для вер.2 по КД нужен профиль 4, а стоит 3. У профиля 4 в ПЛИС
	  * баг: в D_AB_GPIO3R сравнение записано как TYPE == 2'b0100, литерал
	  * двухбитный, старшие цифры отрезаются и условие вырождается в
	  * TYPE == 0. Третья нога не отпускается под MISO, а выдаётся нулём,
	  * и движок читает собственный ноль. Правится на 4'b0100, после чего
	  * сюда ставить 4. Профиль 3 — это вер.2 с платой питания от вер.1,
	  * у него SCLK/MOSI, CS/MISO и обе тангенты переставлены попарно.
	  * Конкретное значение — NPU_GPIO_PROFILE, оно идёт в паре с
	  * NPU_BOARD_CS: профиль решает, какая нога несёт CS, индекс — какой
	  * из двух CS движок опускает, и порознь их держать нельзя.
	  * Из GUI перебивается списком на вкладке Обвязка / НПУ. */
     #ifdef  IS_BORT
	 ppos.creg2.inside.GPIOnR_TYPE = 2;
	 ppos.creg2.inside.Pit = 1;
	 ppos.creg2.inside.Fdiap = 0;
     #else
	 ppos.creg2.inside.GPIOnR_TYPE = NPU_GPIO_PROFILE;
     #endif

	 /* Тангента. Бит Tang: 0 — приём, 1 — передача.
	  *
	  * НПУ — сразу в авто: линию ведёт автомат слота ПЛИС, и сам бит
	  * Tang при Tang_avt = 1 не смотрится вовсе.
	  *
	  * Борт — вручную, по инициализации в ПРМ. Tang_avt обязан быть 0,
	  * иначе Tang не смотрится и автомат уводит линию по такту слота.
	  *
	  * ⚠ Раньше обе строки шли без условия и перебивали Tang_avt = 0,
	  * выставленный в бортовом блоке main.c: modem_init() вызывается
	  * позже, и борт уезжал в авто, хотя в main.c написано обратное. */
	 ppos.creg2.inside.Tang = 0;
     #ifdef  IS_BORT
	 ppos.creg2.inside.Tang_avt = 0;
     #else
	 ppos.creg2.inside.Tang_avt = 1;
     #endif

	 /* Вторая тангента (CONT_TG) — тоже в авто, вслед за первой, но в
	  * ПРОТИВОФАЗЕ. По таблице истинности ячейки штатных состояний два:
	  *   (TX_TG, CONT_TG) = (1,0) — выход передающего канала на антенну,
	  *                      (0,1) — вход приёмного канала с антенны.
	  * Одинаковые значения пары антенну отключают: тракт уводится на
	  * генератор или измерительный прибор.
	  * В ПЛИС NPU_CTX_nCRX = avt ? (bit22 ? tang : ~tang) : bit22, то
	  * есть в авто бит 22 задаёт не значение, а полярность, и ноль даёт
	  * нужное ~tang. */
	 ppos.creg2.inside.NPU_CTX_nCRX = 0;
	 ppos.creg2.inside.NPU_CTX_nCRX_avt = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 8, ppos.creg2.word);


     /* ⚠ Расхождение с knk_krt_bvp (af6139b): там ENABLE_AUTO = 0 для обеих
      * ролей. Мы оставили 1. Коммит коллеги про статистику, а заодно в нём
      * сдвинуты стендовые настройки (num_seg 12->14), поэтому ноль похож на
      * стендовый, а не на требование новой ПЛИС. К тому же apply_role_to_pl()
      * в main.c ставит ENABLE_AUTO = 1 при смене роли — приняв ноль здесь,
      * мы получили бы два разных значения в одной прошивке.
      * Если коллега подтвердит, что новой ПЛИС нужен ноль, править здесь и
      * в apply_role_to_pl(). */
     ppos.creg3.word = 0;
     ppos.creg3.inside.AD9361_ENABLE =1;
	 ppos.creg3.inside.AD9361_TXnRX = 1;
     #ifdef IS_MASTER
	  ppos.creg3.inside.ENABLE_AUTO = 1;
	  ppos.creg3.inside.MasterNSlave = 1;
     #endif
     #ifdef IS_SLAVE
      ppos.creg3.inside.ENABLE_AUTO = 1;
      ppos.creg3.inside.MasterNSlave = 0;
     #endif
	 ppos.creg3.inside.TXnRX_AUTO = 1;
     //ppos.creg3.inside.MasterNSlave = 0;
     ppos.creg3.inside.IS_TDMA = 1;

     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 12, ppos.creg3.word);

	 //ppos.creg4.inside.txnrx_setup = 4500;
	 //ppos.creg4.inside.txnrx_hold =  4500;

	 ppos.creg4.inside.txnrx_setup = 5;
	 ppos.creg4.inside.txnrx_hold  = 25;


	 ppos.creg4.inside.vco_setup   =  25;

/* Число сегментов в своём окне и в окне корреспондента. Поле num_rx_seg
 * заняло бывший резерв creg4: новая ПЛИС по нему знает, сколько сегментов
 * ждать на приёме, и роли получаются зеркальными. */
#ifdef  IS_SLAVE
	 ppos.creg4.inside.num_seg    = 14;
	 ppos.creg4.inside.num_rx_seg = 1;
#else
	 ppos.creg4.inside.num_seg    = 1;
	 ppos.creg4.inside.num_rx_seg = 14;
#endif




	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 16, ppos.creg4.word);









	 ppos.creg5.inside.slot_dur = 3500;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR, 20, ppos.creg5.word);


     ppos.status_on = 1;
     IP4_ADDR(&ppos.status_ip, 192, 168, 250, 22);
     ppos.status_port = 5007;






	 tx_chan0.creg0.inside.bod = 2;
	 tx_chan0.creg0.inside.code = 2;
	 tx_chan0.creg0.inside.mod = 2;
	 tx_chan0.creg0.inside.mux_ad9361_din = 5;
	 tx_chan0.creg0.inside.pkt_len = 0;//544; //576;//4800;//576;
	 tx_chan0.creg0.inside.scr_on = 0;//1;
	 tx_chan0.creg0.inside.gain = 0;
	 tx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, tx_chan0.creg0.word);


  	 tx_chan0.creg2.inside.ad9361_tx_freq = 3500000;//kHz
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 2*4,  (u32) tx_chan0.creg2.word);

  	 uint64_t freq =  tx_chan0.creg2.word;
  	 freq = freq*1000;
  	 ad9361_set_tx_lo_freq (ad9361_phy, freq);


  	 tx_chan0.creg3.inside.ad9361_tx_atten = 0x000003ff&(10000/250); //mdB/250  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   Tx2
  	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 3*4,  (u32) tx_chan0.creg3.word);

     uint32_t atten =  tx_chan0.creg3.inside.ad9361_tx_atten;
     atten*=250;

  	 ad9361_set_tx_attenuation (ad9361_phy, 1, atten);



	 rx_chan0.creg0.inside.bod = 2;
	 rx_chan0.creg0.inside.code = 2;
	 rx_chan0.creg0.inside.mod = 2;
	 rx_chan0.creg0.inside.pkt_len = 0;// 544;//576;// 4800;//576;
	 rx_chan0.creg0.inside.scr_on = 0;//1;
	 rx_chan0.creg0.inside.rcos_filt_on = 1;
	 rx_chan0.creg0.inside.rot_on = 1;
	 rx_chan0.creg0.inside.freq_shift_on = 0;

	 rx_chan0.creg0.inside.run = 0;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0, rx_chan0.creg0.word);


	 rx_chan0.creg0.inside.run = 1;

	 AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 0, rx_chan0.creg0.word);



 	 rx_chan0.creg1.inside.ad9361_rx_freq = 3500000;
     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + RX_CHAN0_PAR_OFFSET, 1*4,  (u32) rx_chan0.creg1.word);

     freq =  rx_chan0.creg1.word;
     freq = freq*1000;
     ad9361_set_rx_lo_freq (ad9361_phy, freq);



     //enable start read pars ad9361
     AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG1_BASEADDR + 15*4, 0,  (u32) 0x00000001);





#ifdef IS_ZED_BOARD

  //d[0]=0x80; d[1]=0x0a;d[2]=0x12;
  //no_os_spi_write_and_read_11bit(&desc_tmp,&d[0],1);

	 ad9361_set_rx_lo_freq (ad9361_phy,3500040000);
	 ad9361_set_tx_lo_freq (ad9361_phy,3500040000);

#endif

 }






