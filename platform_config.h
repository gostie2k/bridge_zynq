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
//#define IS_MASTER 1
//#define IS_OLD_PAR 1
#define IS_SLAVE 1

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
 *    r2 = + master-kick (bridgeif_fdb_update_src) */
#define FW_VERSION   "wdt-robust-r2"

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
