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

/* data TX; telemetry goes to MM_S_0, all RX too */
#define FIFO_NPR_TX_DEV_BASE_ADDR XPAR_AXI_FIFO_MM_S_2_BASEADDR



#define BRIDGE 1

//#define IS_BORT 1

#define IS_MASTER 1
//#define IS_OLD_PAR 1
//#define IS_SLAVE

//#define IS_ANTSDR
#define IS_MAKET_PKRV
//#define IS_ZED_BOARD


#define AD9361_SPI_VIA_ENGINE 1

/* Set per modem by hand before flashing */
#ifndef DEVICE_ID_DEFAULT
#define DEVICE_ID_DEFAULT  1
#endif



#define RX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_BASEADDR
#define TX_CORE_BASEADDR XPAR_PPOS_DSP_0_S01_AXI_BASEADDR
//XPAR_AXI_AD9361_0_HIGHADDR



#define AXI_CNTRL_REG_BASEADDR  XPAR_PPOS_DSP_0_S00_AXI_BASEADDR
/* peleng: own AXI slave */
#define AXI_CNTRL_REG1_BASEADDR  XPAR_PPOS_DSP_0_S04_AXI_BASEADDR
#define SPI_ENGINE_AD9361_BASEADDR  XPAR_PPOS_DSP_0_S02_AXI_BASEADDR
#define SPI_ENGINE_NPU_BASEADDR  XPAR_PPOS_DSP_0_S03_AXI_BASEADDR

/* NPU cell variant: 2 = v2, 1 = v1, 0 = old. Port maps are incompatible.
 * Only truth is tested in code, so 1 and 2 behave the same. */
#define NPU_CELL_NEW 2

/* Pin profile and CS, both derived from the cell variant on purpose:
 * if they disagree the cell just goes quiet. */
#if NPU_CELL_NEW
#define NPU_BOARD_CS     1
#define NPU_GPIO_PROFILE 3
#else
#define NPU_BOARD_CS     0
#define NPU_GPIO_PROFILE 1
#endif


/* Is the NPU cell wired to this modem.
 * 0 leaves MAX7301 in shutdown: do not use it if anything in this
 * modem is powered or enabled through the cell. */
#define NPU_CELL_PRESENT 0


/* 1 Hz SPI poll of the cell; 0 keeps the bus quiet for ILA work */
#define NPU_STATUS_POLL 1


/* Both chips in Mode 3. Modes cannot be mixed while the bridge CS is
 * an inversion of CS1: the engine changes CPOL before CS goes low. */
#define NPU_SPI_MODE3 1

/* Probe the SC18IS604 bridge at startup and print to UART */
#define NPU_BRIDGE_DEBUG 1

/* 1 = bridge has its own CS. Needs an FPGA build with AB_GPIO7R and
 * R18 removed on the cell board, both at once. */
#define NPU_BRIDGE_OWN_CS 1

/* bridge sits on cs2, which is bit 0 of the mask */
#define NPU_BRIDGE_CS 0

#if NPU_BRIDGE_DEBUG && !NPU_BRIDGE_OWN_CS && !NPU_SPI_MODE3
#error "bridge via inverted CS1 needs NPU_SPI_MODE3: SPI modes cannot be mixed"
#endif

/* AB_GPIO7R is an output only in profile 3; otherwise the CS floats */
#if NPU_BRIDGE_OWN_CS && (NPU_GPIO_PROFILE != 3)
#error "NPU_BRIDGE_OWN_CS needs NPU_GPIO_PROFILE == 3, else AB_GPIO7R floats"
#endif

/* Bort build: no NPU cell at all */
#ifdef IS_BORT
#undef  NPU_CELL_PRESENT
#define NPU_CELL_PRESENT 0
#endif

/* No cell: poll and bridge probe have nothing to talk to */
#if !NPU_CELL_PRESENT
#undef  NPU_STATUS_POLL
#define NPU_STATUS_POLL  0
#undef  NPU_BRIDGE_DEBUG
#define NPU_BRIDGE_DEBUG 0
#endif


#define RX_CHAN0_DEB_W 5

#define FW_VERSION   "ASYM-14-1"   /* slave 14 segments per slot, master 1 */

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

/* ⚠ ignored: watchdog.h is included first in main.c and sets it to 1 */
#ifndef WDT_ENABLED_DEFAULT
#define WDT_ENABLED_DEFAULT  0
#endif


#endif
