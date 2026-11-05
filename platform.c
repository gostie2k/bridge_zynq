/* Copyright (C) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
 * SPDX-License-Identifier: MIT */


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




void knk_init_platform(XUartPs* ptr_uart1_ps, XGpioPs* ptr_gpio_ps)
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

}






//void cleanup_platform()
//{
//    disable_caches();
//}
