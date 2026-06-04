#ifndef SDT
#ifdef __arm__

#include "xparameters.h"
#include "xparameters_ps.h"	/* defines XPAR values */
#include "xil_cache.h"
#include "xscugic.h"
#include "lwip/udp.h"  // «аменено на udp.h дл€ работы с UDP
#include "xil_printf.h"
#include "platform.h"
#include "platform_config.h"
#include "netif/xadapter.h"
#ifdef PLATFORM_ZYNQ
#include "xscutimer.h"

//#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TIMER_DEVICE_ID		XPAR_SCUTIMER_DEVICE_ID
#define INTC_BASE_ADDR		XPAR_SCUGIC_0_CPU_BASEADDR
#define INTC_DIST_BASE_ADDR	XPAR_SCUGIC_0_DIST_BASEADDR
#define TIMER_IRPT_INTR		XPAR_SCUTIMER_INTR

#define RESET_RX_CNTR_LIMIT	400

static XScuTimer TimerInstance;

#ifndef USE_SOFTETH_ON_ZYNQ
static int ResetRxCntr = 0;
#endif

extern struct netif *echo_netif;

#if LWIP_DHCP==1
volatile int dhcp_timoutcntr = 24;
void dhcp_fine_tmr();
void dhcp_coarse_tmr();
#endif

void
timer_callback(XScuTimer * TimerInstance)
{
	static int DetectEthLinkStatus = 0;
	/* we need to call the timer at intervals for DHCP and Ethernet link detection */
	static int odd = 1;
#if LWIP_DHCP==1
    static int dhcp_timer = 0;
#endif
	DetectEthLinkStatus++;

	odd = !odd;
#ifndef USE_SOFTETH_ON_ZYNQ
	ResetRxCntr++;
#endif
	if (odd) {
#if LWIP_DHCP==1
		dhcp_timer++;
		dhcp_timoutcntr--;
#endif
#if LWIP_DHCP==1
		dhcp_fine_tmr();
		if (dhcp_timer >= 120) {
			dhcp_coarse_tmr();
			dhcp_timer = 0;
		}
#endif
	}

	/* Reset Rx path if no Rx traffic is detected for a period of time (to work around hardware bug) */
#ifndef USE_SOFTETH_ON_ZYNQ
	if (ResetRxCntr >= RESET_RX_CNTR_LIMIT) {
		xemacpsif_resetrx_on_no_rxdata(echo_netif);
		ResetRxCntr = 0;
	}
#endif
	/* For detecting Ethernet phy link status periodically */
	if (DetectEthLinkStatus == ETH_LINK_DETECT_INTERVAL) {
		eth_link_detect(echo_netif);
		DetectEthLinkStatus = 0;
	}

	XScuTimer_ClearInterruptStatus(TimerInstance);
}

void platform_setup_timer(void)
{
	int Status = XST_SUCCESS;
	XScuTimer_Config *ConfigPtr;
	int TimerLoadValue = 0;

	ConfigPtr = XScuTimer_LookupConfig(TIMER_DEVICE_ID);
	Status = XScuTimer_CfgInitialize(&TimerInstance, ConfigPtr,
			ConfigPtr->BaseAddr);
	if (Status != XST_SUCCESS) {

		xil_printf("In %s: Scutimer Cfg initialization failed...\r\n",
		__func__);
		return;
	}

	Status = XScuTimer_SelfTest(&TimerInstance);
	if (Status != XST_SUCCESS) {
		xil_printf("In %s: Scutimer Self test failed...\r\n",
		__func__);
		return;

	}

	XScuTimer_EnableAutoReload(&TimerInstance);
	/*
	 * Set for 250 milliseconds timeout.
	 */
	TimerLoadValue = XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 8;

	XScuTimer_LoadTimer(&TimerInstance, TimerLoadValue);
	return;
}

int platform_setup_interrupts(XScuGic* ptr_scugic, XGpioPs* ptr_gpio_ps,  XGpioPs_Handler FuncPointer)
{
	Xil_ExceptionInit();

	//XScuGic_DeviceInitialize(INTC_DEVICE_ID);

	XScuGic_Config *IntcConfig;
	int Status;

	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
			if (NULL == IntcConfig) {
				return XST_FAILURE;
			}
    Status = XScuGic_CfgInitialize(ptr_scugic, IntcConfig, IntcConfig->CpuBaseAddress);
			if (Status != XST_SUCCESS) {
				return XST_FAILURE;
			}


	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	//Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
	//		(Xil_ExceptionHandler)XScuGic_DeviceInterruptHandler,
	//		(void *)INTC_DEVICE_ID);

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler,  ptr_scugic);

	/*
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */


	Status = XScuGic_Connect(ptr_scugic, GPIO_INTERRUPT_ID,(Xil_ExceptionHandler)XGpioPs_IntrHandler,(void *) ptr_gpio_ps);
		if (Status != XST_SUCCESS) {
			return Status;
		}


    XGpioPs_SetCallbackHandler((XGpioPs*) ptr_gpio_ps, (void *) ptr_gpio_ps, FuncPointer);

	//st_mod
	//XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+63);
	//XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+63, XGPIOPS_IRQ_TYPE_EDGE_RISING);

		//rx_pkt_int
	XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+61, XGPIOPS_IRQ_TYPE_EDGE_RISING);
	XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+61);


	//tx_cntrl_int
    //XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+60);
    //XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+60, XGPIOPS_IRQ_TYPE_EDGE_RISING);


	//tx_cntrl_end_int
    //XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+59);
    //XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+59, XGPIOPS_IRQ_TYPE_EDGE_RISING);


	/* Enable the interrupt for the GPIO device. */
	XScuGic_Enable(ptr_scugic, GPIO_INTERRUPT_ID);


//timer int

    Status = XScuGic_Connect(ptr_scugic, TIMER_IRPT_INTR,
	                             (Xil_ExceptionHandler)timer_callback,
	                             (void *)&TimerInstance);
    if (Status != XST_SUCCESS) {
	        return Status;
	    }



//	XScuGic_RegisterHandler(INTC_BASE_ADDR, TIMER_IRPT_INTR,
//				(Xil_ExceptionHandler)timer_callback,
//					(void *)&TimerInstance);



	    /*
	 * Enable the interrupt for scu timer.
	 */
	//XScuGic_EnableIntr(INTC_DIST_BASE_ADDR, TIMER_IRPT_INTR);
	XScuGic_Enable(ptr_scugic, TIMER_IRPT_INTR);


	//Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);



	 // Ќазначаем таймеру максимальный приоритет (например, 0x08)
	    // 0x3 означает, что прерывание срабатывает по высокому уровню (High Level)
	  //  XScuGic_SetPriorityTriggerType(ptr_scugic, TIMER_IRPT_INTR, 0x08, 0x3);

	    // Ќазначаем GPIO более низкий приоритет (например, 0xA0)
	    // 0x1 означает, что прерывание срабатывает по нарастающему фронту (Rising Edge)
	  //  XScuGic_SetPriorityTriggerType(ptr_scugic, GPIO_INTERRUPT_ID, 0xA0, 0x1);




	return XST_SUCCESS;
}

void platform_enable_interrupts()
{
	/*
	 * Enable non-critical exceptions.
	 */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	XScuTimer_EnableInterrupt(&TimerInstance);
	XScuTimer_Start(&TimerInstance);
	return;
}

//void init_platform()
//{
//	Xil_ICacheEnable();
//	Xil_DCacheEnable();


//	platform_setup_timer();
//	platform_setup_interrupts();

//	return;
//}

void cleanup_platform()
{
	Xil_ICacheDisable();
	Xil_DCacheDisable();
	return;
}
#endif
#endif
#endif /* NOT SDT */
