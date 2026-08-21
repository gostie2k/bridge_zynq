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

//#define IS_BORT 1

//#define IS_MASTER 1
//#define IS_OLD_PAR 1
#define IS_SLAVE 1

#define IS_ANTSDR
//#define IS_MAKET_PKRV
//#define IS_ZED_BOARD

/* Unique device ID (role-independent). Set manually on each modem before
 * flashing: 1st modem=1, 2nd=2, etc. Used for addressed broadcast commands
 * (0xA0 group=5) and embedded in every 188-byte radio segment. */
#ifndef DEVICE_ID_DEFAULT
#define DEVICE_ID_DEFAULT  2
#endif

#define RX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_BASEADDR
#define TX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_HIGHADDR



#define AXI_CNTRL_REG_BASEADDR  XPAR_PPOS_DSP_0_S00_AXI_BASEADDR
/* Peleng: separate AXI slave for the pulse signal */
#define AXI_CNTRL_REG1_BASEADDR  XPAR_PPOS_DSP_0_S04_AXI_BASEADDR
#define SPI_ENGINE_AD9361_BASEADDR  XPAR_PPOS_DSP_0_S02_AXI_BASEADDR
#define SPI_ENGINE_NPU_BASEADDR  XPAR_PPOS_DSP_0_S03_AXI_BASEADDR

#define NPU_COM_BOARD_CS 0
#define NPU_POW_BOARD_CS 1


/* ===================================================================
 *  Firmware version / role + WDT default (WDT-stability merge)
 * =================================================================== */
#define FW_VERSION   "wdt-robust-r11"

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

/* WDT on by default (1=live). Runtime toggle - UDP 0x90 0x4C. */
#ifndef WDT_ENABLED_DEFAULT
#define WDT_ENABLED_DEFAULT  0
#endif


#endif
