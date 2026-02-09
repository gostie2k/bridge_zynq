/******************************************************************************
* Copyright (C) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#include "platform.h"
#include "xil_cache.h"

//#ifndef SDT
#include "platform_config.h"
//#endif


int Status;

/*
 * Uncomment one of the following two lines, depending on the target,
 * if ps7/psu init source files are added in the source directory for
 * compiling example outside of SDK.
 */
/*#include "ps7_init.h"*/
/*#include "psu_init.h"*/

#ifdef STDOUT_IS_16550
 #include "xuartns550_l.h"

 #define UART_BAUD 9600
#endif

void
enable_caches()
{
	 Xil_ICacheEnable();
	 Xil_DCacheEnable();


#ifdef __PPC__
    Xil_ICacheEnableRegion(CACHEABLE_REGION_MASK);
    Xil_DCacheEnableRegion(CACHEABLE_REGION_MASK);
#elif __MICROBLAZE__
#ifdef XPAR_MICROBLAZE_USE_ICACHE
    Xil_ICacheEnable();
#endif
#ifdef XPAR_MICROBLAZE_USE_DCACHE
    Xil_DCacheEnable();
#endif
#endif
}

void
disable_caches()
{
	 Xil_DCacheDisable();
	 Xil_ICacheDisable();

#ifdef __MICROBLAZE__
#ifdef XPAR_MICROBLAZE_USE_DCACHE
    Xil_DCacheDisable();
#endif
#ifdef XPAR_MICROBLAZE_USE_ICACHE
    Xil_ICacheDisable();
#endif
#endif
}

s32 init_uart1(XUartPs* ptr_uart_ps)
{
   	/*
	 * Initialize the UART driver so that it's ready to use
	 * Look up the configuration in the config table and then initialize it.
	 */

    XUartPs_Config *Config1;
	Config1 = XUartPs_LookupConfig(UART_DEVICE_ID);
	Status = XUartPs_CfgInitialize(ptr_uart_ps, Config1, Config1->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

    XUartPs_SetBaudRate(ptr_uart_ps, 115200);
   return XST_SUCCESS ;
}

s32 init_gpio(XGpioPs* ptr_gpio_ps)
{
   	/*
	 * Initialize the GPIO driver so that it's ready to use
	 * Look up the configuration in the config table and then initialize it.
	 */

	    XGpioPs_Config *ConfigPtr;
	    ConfigPtr = XGpioPs_LookupConfig(GPIO_DEV_ID);
	    XGpioPs_CfgInitialize(ptr_gpio_ps, ConfigPtr, ConfigPtr->BaseAddr);

		/* Run a self-test on the GPIO device. */
		Status = XGpioPs_SelfTest(ptr_gpio_ps);
		if (Status != XST_SUCCESS) {
			return XST_FAILURE;
		}

		/* Set the direction for the specified pin to be output. */
	    for (int i=0;i<32;i++)
	    {
		XGpioPs_SetDirectionPin(ptr_gpio_ps, 54+i, 1);
		XGpioPs_SetOutputEnablePin(ptr_gpio_ps, 54+i, 1);
	    XGpioPs_WritePin(ptr_gpio_ps, 54+i, 0);
	    }

	    /* Set the direction for the specified pin to be input */
	    for (int i=32;i<64;i++)
	    {
		XGpioPs_SetDirectionPin(ptr_gpio_ps, 54+i, 0x0);
	    }

	    return XST_SUCCESS ;
}

s32 init_ScuGic(XScuGic* ptr_scugic, XGpioPs* ptr_gpio_ps,  XGpioPs_Handler FuncPointer)
{

	//GIC config
	XScuGic_Config *IntcConfig;
	Xil_ExceptionInit();
	//initialize the GIC
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


	//st_mod
	XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+63);
	XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+63, XGPIOPS_IRQ_TYPE_EDGE_RISING);

		//rx_pkt_int
	XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+61);
	XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+61, XGPIOPS_IRQ_TYPE_EDGE_RISING);

	//tx_cntrl_int
    XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+60);
    XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+60, XGPIOPS_IRQ_TYPE_EDGE_RISING);


	//tx_cntrl_end_int
    XGpioPs_IntrEnablePin(ptr_gpio_ps, 54+59);
    XGpioPs_SetIntrTypePin(ptr_gpio_ps, 54+59, XGPIOPS_IRQ_TYPE_EDGE_RISING);





	XGpioPs_SetCallbackHandler((XGpioPs*) ptr_gpio_ps, (void *) ptr_gpio_ps, FuncPointer);








	/* Enable the interrupt for the GPIO device. */
	XScuGic_Enable(ptr_scugic, GPIO_INTERRUPT_ID);


	/*
		 * Enable the interrupt for scu timer.
		 */
   //XScuGic_EnableIntr(INTC_DIST_BASE_ADDR, TIMER_IRPT_INTR);


	/* Enable interrupts in the Processor. */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	return XST_SUCCESS ;
}


void knk_init_platform(XUartPs* ptr_uart1_ps, XGpioPs* ptr_gpio_ps, XScuGic* ptr_scugic, XGpioPs_Handler FuncPointer)
{
    /*
     * If you want to run this example outside of SDK,
     * uncomment one of the following two lines and also #include "ps7_init.h"
     * or #include "ps7_init.h" at the top, depending on the target.
     * Make sure that the ps7/psu_init.c and ps7/psu_init.h files are included
     * along with this example source files for compilation.
     */
    /* ps7_init();*/
    /* psu_init();*/
    //enable_caches();
    init_uart1(ptr_uart1_ps);
    init_gpio(ptr_gpio_ps);
    init_ScuGic(ptr_scugic, ptr_gpio_ps, FuncPointer);
}






//void cleanup_platform()
//{
//    disable_caches();
//}
