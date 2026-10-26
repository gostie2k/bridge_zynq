#ifndef __PLATFORM_CONFIG_H_
#define __PLATFORM_CONFIG_H_

#include "xparameters.h"

//#define  STDOUT_IS_PS7_UART


#define UART_BASEADDR XPAR_XUARTPS_0_BASEADDR
#define UART_DEVICE_ID XPAR_XUARTPS_0_DEVICE_ID

#define  GPIO_DEV_ID    XPAR_PS7_GPIO_0_DEVICE_ID
#define  GPIO_INTERRUPT_ID	  52
#define  INTC_DEVICE_ID      XPAR_SCUTIMER_DEVICE_ID
#define  PLATFORM_EMAC_BASEADDR XPAR_XEMACPS_0_BASEADDR

#define ETH_LINK_DETECT_INTERVAL 4

#define FIFO_DEV_ID XPAR_AXI_FIFO_MM_S_0_DEVICE_ID
#define FIFO_RX_DEV_BASE_ADDR XPAR_AXI_FIFO_MM_S_0_BASEADDR
#define FIFO_DEB_DEV_BASE_ADDR XPAR_AXI_FIFO_MM_S_1_BASEADDR

/* Вторая передающая FIFO (добавлена в ПЛИС 2026-09-30).
 * Разделение: телеметрия уходит в старую FIFO_RX (MM_S_0), данные —
 * в эту. Приём по-прежнему весь через MM_S_0. */
#define FIFO_NPR_TX_DEV_BASE_ADDR XPAR_AXI_FIFO_MM_S_2_BASEADDR



#define BRIDGE 1

//#define IS_BORT 1

//#define IS_MASTER 1
//#define IS_OLD_PAR 1
#define IS_SLAVE 1

#define IS_ANTSDR
//#define IS_MAKET_PKRV
//#define IS_ZED_BOARD

/* Уникальный ID устройства (НЕ зависит от роли). Задаётся вручную на КАЖДОМ
 * модеме перед прошивкой: 1-й модем=1, 2-й=2, и т.д. Используется для адресных
 * broadcast-команд (0xA0 group=5) и кладётся в каждый 188-байтный радио-сегмент. */

#ifndef DEVICE_ID_DEFAULT
#define DEVICE_ID_DEFAULT  1
#endif



#define RX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_BASEADDR
#define TX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_HIGHADDR



#define AXI_CNTRL_REG_BASEADDR  XPAR_PPOS_DSP_0_S00_AXI_BASEADDR
/* Пеленг (правки коллеги): отдельный AXI-слейв под импульсный сигнал */
#define AXI_CNTRL_REG1_BASEADDR  XPAR_PPOS_DSP_0_S04_AXI_BASEADDR
#define SPI_ENGINE_AD9361_BASEADDR  XPAR_PPOS_DSP_0_S02_AXI_BASEADDR
#define SPI_ENGINE_NPU_BASEADDR  XPAR_PPOS_DSP_0_S03_AXI_BASEADDR

/* ===================== Ячейка НПУ =====================
 * Выбор варианта железа. В обоих вариантах это одна MAX7301 на
 * единственном chip select, но карты портов несовместимы, поэтому
 * промежуточных состояний нет — только 1 или 0.
 *
 *   1 — новая ячейка распределения питания и управления
 *       ЕРВА.468349.020: MAX7301 (DD9), протокол сопряжения от
 *       NN октября 2026 г. CS2 для моста SPI-I2C формируется на самой
 *       ячейке как /CS1, контакт 5 разъёма XP3 — CONT_TG, а не второй
 *       chip select.
 *
 *   0 — прежняя ячейка. По КД микросхем было две (com board + pow
 *       board с преселектором на своём CS), но от pow board отказались
 *       и преселектор перенесли на com board, на порты P26 и P28.
 * ====================================================== */
#define NPU_CELL_NEW 2

/* Профиль распиновки AB_GPIO1R..6R и chip select ячейки НПУ. Обе
 * величины привязаны к варианту платы, поэтому выводятся из
 * NPU_CELL_NEW, а не задаются отдельно: расходясь, они дают ровно одну
 * картину — SCLK и MOSI идут, CS не опускается, ячейка молчит.
 *
 * В ПЛИС {npu_ant_cs1, npu_ant_cs2} = spi01_cs, то есть cs1 — бит 1
 * маски, cs2 — бит 0.
 *
 *   новая ячейка: профиль 0011, единственный CS на AB_GPIO3R — это cs1,
 *     индекс 1 (ILA 16.10.2026);
 *   прежняя ячейка: профиль 0001, CS оставшейся микросхемы (бывшая com
 *     board) на AB_GPIO6R — это cs2, индекс 0. Так и было в рабочей
 *     прошивке: NPU_COM_BOARD_CS = 0.
 *
 * На плате сигнал к MAX7301 подписан CS1 — это НЕ npu_ant_cs1 ПЛИС. */
#if NPU_CELL_NEW
#define NPU_BOARD_CS     1
#define NPU_GPIO_PROFILE 3
#else
#define NPU_BOARD_CS     0
#define NPU_GPIO_PROFILE 1
#endif


/* Секундный опрос ячейки по SPI из главного цикла. Снимает индикаторы
 * панелей и выходы (11 чтений = 22 кадра раз в секунду), чтобы поле
 * status_npu в пакете статуса не уходило нулями: func_type = 17 по UDP
 * никто не вызывает.
 *
 *   1 — опрос идёт;
 *   0 — молчит, по SPI летит только инициализация MAX7301 при старте и
 *       то, что запрошено командой. Нужно, когда шину смотрят на ILA:
 *       иначе пачка раз в секунду мешает ловить свой обмен, а поля
 *       status_npu и выходы в статусе остаются нулевыми. */
#define NPU_STATUS_POLL 1


/* Режим SPI на шине ячейки НПУ.
 *
 * MAX7301 работает и в Mode 0, и в Mode 3 — он защёлкивает данные по
 * нарастающему фронту. У моста SC18IS604 выбора нет, он только Mode 3.
 *
 * Смешивать режимы на этой шине нельзя, пока CS моста получается
 * инверсией CS1. Движок кладёт команду CONFIG в голову очереди кадра
 * (spi_engine.c:267, вызов из spi_engine_compile_message), то есть
 * CPOL меняется ДО CS_HIGH/CS_LOW. В этот момент все CS подняты, CS1
 * высокий, значит CS2 низкий и мост выбран — смена CPOL подняла бы
 * SCLK прямо тогда, а в Mode 3 это фронт выборки, и мост защёлкнул бы
 * мусорный бит.
 *
 *   1 — обе микросхемы в Mode 3, глитчей нет;
 *   0 — макс в Mode 0, как было. Несовместимо с NPU_BRIDGE_DEBUG. */
#define NPU_SPI_MODE3 1

/* Отладка моста SPI->I2C SC18IS604 (DD11) на плате ячейки.
 *
 * Пока отдельный CS на мост не выведен, он адресуется через тот же
 * CS1: при chip_select = 0 движок опускает cs[0], а cs[1] — это и есть
 * CS1 — остаётся в единице весь обмен, значит CS2 низкий и мост
 * выбран. MAX7301 при этом не выбирается ни разу, а его DOUT отрезан
 * от MISO буфером DD8 (вход OE сидит на CS1), так что драки нет.
 *
 *   1 — при старте, после инициализации MAX7301, идёт разбор моста:
 *       строка версии по команде 0xFE, настройка I2CClock и I2CTO,
 *       перебор восьми INA226 по адресам 0x40..0x47. Всё печатается
 *       в UART;
 *   0 — мост не трогается вообще, лишних кадров по SPI нет. */
#define NPU_BRIDGE_DEBUG 1

/* Собственный chip select у моста.
 *
 *   0 — переходный вариант, как на плате сейчас: CS моста получен
 *       инверсией CS1 (DA2, 74LVC1GU04GW), поэтому кадр моста
 *       ограничивается обращением к максу, а режим SPI обязан быть
 *       одинаковым у обеих микросхем;
 *   1 — у моста свой CS с отдельного контакта. Обёртки из No-Op
 *       уходят, требование NPU_SPI_MODE3 для макса снимается — при
 *       раздельных CS в покое обе микросхемы не выбраны, и смена CPOL
 *       перед кадром никого не задевает.
 *
 * ⚠ Единица требует двух вещей вне прошивки, обеих сразу:
 *   1) прошивка ПЛИС с седьмой линией AB_GPIO7R. В ppos_dsp.v она
 *      объявлена inout-портом, жёстко привязана к npu_ant_cs2
 *      (M_AB_GPIO7R) и разрешена на выход только в профиле 3:
 *      D_AB_GPIO7R = (creg2_GPIOnR_TYPE == 4'b0011). Значит NPU_GPIO_PROFILE
 *      обязан оставаться равным 3: при любом другом номере седьмая
 *      линия уйдёт в Z, CS моста повиснет, и мост замолчит без
 *      единого признака почему;
 *   2) ⚠ разрыв по R18 (0 Ом) на плате ячейки. R18 соединяет
 *      выход инвертора DA2 (цепь CS2) с ногой 8 CS моста DD11.
 *      Его выпаивают, а на пятачок со стороны DD11 заводят
 *      провод с нового CS модема. Без разрыва на одну цепь встанут
 *      два выхода — инвертор и AB_GPIO7R. Сам DA2 можно оставить:
 *      после разрыва его выход упирается в пустой пятачок.
 *
 * Чтению переход не мешает: MISO моста (DD11.14 через R23)
 * сидит на общей линии напрямую, а выход макса идёт через буфер
 * DD8, у которого OE висит на CS1. Движок опускает ровно один CS за
 * кадр, так что при выбранном мосте CS1 высокий и DD8 в Z, а при
 * выбранном максе молчит мост. Линия всегда у кого-то одного. */
#define NPU_BRIDGE_OWN_CS 1

/* Индекс chip select моста для движка. В ПЛИС
 * {npu_ant_cs1, npu_ant_cs2} = spi01_cs, то есть cs1 — бит 1 маски,
 * cs2 — бит 0. Мост садится на cs2, отсюда 0.
 *
 * Значение одинаково в обоих вариантах, и это не совпадение. При
 * инверсии индекс 0 опускал cs2 (никуда не выведенный) и оставлял cs1
 * в единице — а высокий CS1 и выбирал мост через DA2. При своём CS
 * тот же индекс 0 опускает cs2 на AB_GPIO7R, то есть прямо на ногу
 * моста. Поэтому при переходе bridge_desc_init() не меняется вовсе. */
#define NPU_BRIDGE_CS 0

#if NPU_BRIDGE_DEBUG && !NPU_BRIDGE_OWN_CS && !NPU_SPI_MODE3
#error "мост через инверсию CS1 требует NPU_SPI_MODE3: режимы SPI смешивать нельзя"
#endif

/* Бортовой вариант: ячейки НПУ нет вовсе.
 *
 * В профиле 2 мультиплексор ПЛИС отдаёт все шесть ног бортовым сигналам
 * (tang, air_diap, ~tang, ~Pit, ~air_diap), а SCLK/MOSI/CS движка до
 * пинов не доходят — остаются в Z. AB_GPIO7R (CS моста) в профиле 2
 * тоже в Z. То есть кадры никуда не уезжают и ничего не дёргают, но
 * гонять их незачем: в UART ложился полный лог инициализации MAX7301
 * с [FAIL] на каждой строке и восемь «INA226 не ответил», а это
 * читается как неисправность. Плюс 22 кадра в секунду в пустоту.
 *
 * Сами NPU_CELL_NEW / NPU_BOARD_CS / NPU_GPIO_PROFILE / NPU_SPI_MODE3 /
 * NPU_BRIDGE_OWN_CS / NPU_BRIDGE_CS не переопределяем: с отключённой
 * инициализацией и опросом они ни на что не влияют. */
#ifdef IS_BORT
#undef  NPU_STATUS_POLL
#define NPU_STATUS_POLL  0
#undef  NPU_BRIDGE_DEBUG
#define NPU_BRIDGE_DEBUG 0
#endif


#define RX_CHAN0_DEB_W 5

/* ===================
 *  Firmware version
 * =================== */
#define FW_VERSION   "ASYM-14-1"   /* слейв 14 сегментов за слот, мастер 1 */

#if defined(IS_MASTER) && !defined(IS_SLAVE)
  #define FW_ROLE_STR "MASTER"
  #define FW_ROLE_ID  1u
#elif defined(IS_SLAVE) && !defined(IS_MASTER)
  #define FW_ROLE_STR "SLAVE"
  #define FW_ROLE_ID  2u
#else
  #define FW_ROLE_STR "ROLE?!"
  #define FW_ROLE_ID  0u
#endif

#define FW_BUILD_STR  FW_VERSION " " FW_ROLE_STR " (" __DATE__ " " __TIME__ ")"

/* WDT включён по умолчанию (1=боевой). Рантайм-тумблер — UDP 0x90 0x4C. */
#ifndef WDT_ENABLED_DEFAULT
#define WDT_ENABLED_DEFAULT  0
#endif


#endif
