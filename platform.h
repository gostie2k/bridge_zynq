/******************************************************************************
* Copyright (C) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/
#ifndef __PLATFORM_H_
#define __PLATFORM_H_

#ifndef SDT
#include "platform_config.h"
#endif


#include "xuartps.h"
#include "xgpiops.h"
#include "xscugic.h"

#include "xil_exception.h"
//#include "xqspips.h"
//#include "Xscugic.h"
//#include "Xil_exception.h"
#include "xil_printf.h"
//#include "xsdps.h"
//#include "xscutimer.h"

#include "ad9361/no_os_spi.h"
#include "ad9361/app_config.h"
#include "ad9361/ad9361.h"
#include "ad9361/ad9361_api.h"
//#include "ad9361/parameters.h"

#include "xil_io.h"
#include "ad9361/no_os_axi_io.h"
#include "axi_adc_core/axi_adc_core.h"
#include "axi_dac_core/axi_dac_core.h"
#include "axi_dmac/axi_dmac.h"





void knk_init_platform(XUartPs* ptr_uart1_ps, XGpioPs* ptr_gpio_ps);
s32 init_uart1(XUartPs* ptr_uart_ps);
s32 init_gpio(XGpioPs* ptr_gpio_ps);





//void cleanup_platform();

void platform_setup_timer();
void platform_enable_interrupts();
int platform_setup_interrupts(XScuGic* ptr_scugic, XGpioPs* ptr_gpio_ps,  XGpioPs_Handler FuncPointer);

#endif
