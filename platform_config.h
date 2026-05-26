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


#define BRIDGE 1
#define IS_MASTER 1
//#define IS_OLD_PAR 1
//#define IS_SLAVE 1

#define IS_ANTSDR
//#define IS_MAKET_PKRV
//#define IS_ZED_BOARD

#define RX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_BASEADDR
#define TX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_HIGHADDR



#define AXI_CNTRL_REG_BASEADDR  XPAR_PPOS_DSP_0_S00_AXI_BASEADDR
#define SPI_ENGINE_AD9361_BASEADDR  XPAR_PPOS_DSP_0_S02_AXI_BASEADDR
#define SPI_ENGINE_NPU_BASEADDR  XPAR_PPOS_DSP_0_S03_AXI_BASEADDR

#define NPU_COM_BOARD_CS 0
#define NPU_POW_BOARD_CS 1


/* ===================================================================
 *  Firmware version / role indication
 *  ------------------------------------------------------------------
 *  Чтобы исключить «а тот ли билд залит» — версия и роль выводятся:
 *    - по UART баннером при старте (см. main.c после knk_init_platform);
 *    - по Ethernet командой 0x90 0x48 (см. cmd_udp.c).
 *  ВНИМАНИЕ: __DATE__/__TIME__ в FW_BUILD_STR отражают время компиляции
 *  cmd_udp.c (где раскрывается макрос), а НЕ всего образа. При инкрементальной
 *  сборке Vitis, если cmd_udp.c не менялся, таймстамп ОСТАЁТСЯ СТАРЫМ даже после
 *  правки других файлов → по таймстампу судить о билде НЕЛЬЗЯ.
 *  Надёжный признак — строка FW_VERSION: бампай её при КАЖДОМ значимом
 *  изменении (смена platform_config.h форсит пересборку cmd_udp.c → 0x48 отдаёт
 *  новую строку). Роль выводится из IS_MASTER/IS_SLAVE автоматически.
 *    r1 = slave-kick (radio_linkoutput)
 *    r2 = + master-kick (bridgeif_fdb_update_src)
 *    r3 = + инструментация (0x49: xemacif длительность, счётчик kick)
 *    r4 = + рейт-кап radio TX (A: держим master ниже триггера лок-апа GEM-RX)
 *    r5 = кап-дефолт 1.2M→1.0M (1.2M на стенде ещё ронял master; 1.0M держит 8+ мин)
 *    r6 = DIAG (B): счётчик GEM-ошибок 0x4B (+ врезка в BSP xemacpsif_hw.c)
 *    r7 = DIAG (B): кап ВЫКЛ по дефолту, чтобы мастер свободно виснул и ловить
 *         0x4B. ВРЕМЕННО — после фикса B вернуть RADIO_TX_CAP_BPS_DEFAULT=1000000u
 *    r8 = DIAG (B): section-маркер в process_sent_bds (TX-done ISR) → crash_section=12.
 *         РЕЗУЛЬТАТ: фриз дал crash_section=1(LWIP_INPUT), НЕ 12 → process_sent_bds исключён.
 *    r9 = DIAG (B) итерация 2: маркеры в low_level_output(10), netif->input/мост(7),
 *         emacps_recv_handler RX-ISR(8). Следующий фриз сузит место. Кап ВЫКЛ. */
#define FW_VERSION   "wdt-robust-r9-diag"

/* A: дефолтный кап radio TX в битах/с по ethernet-данным (0 = выключен).
 * Рабочее значение — 1000000u (master стабилен 8+ мин под 2M). СЕЙЧАС 0 (выкл)
 * для диагностики B: мастер должен свободно виснуть, чтобы ловить причину по 0x4B.
 * Тюнится в рантайме по UDP 0x90 0x4A. ВЕРНУТЬ 1000000u после фикса B! */
#define RADIO_TX_CAP_BPS_DEFAULT  0u

#if defined(IS_MASTER) && !defined(IS_SLAVE)
  #define FW_ROLE_STR "MASTER"
  #define FW_ROLE_ID  1u
#elif defined(IS_SLAVE) && !defined(IS_MASTER)
  #define FW_ROLE_STR "SLAVE"
  #define FW_ROLE_ID  2u
#else
  /* Ни одна или обе роли определены — это мисконфиг, явно сигналим. */
  #define FW_ROLE_STR "ROLE?!"
  #define FW_ROLE_ID  0u
#endif

#define FW_BUILD_STR  FW_VERSION " " FW_ROLE_STR " (" __DATE__ " " __TIME__ ")"


#endif
