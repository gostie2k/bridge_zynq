
#include <inttypes.h>
#include "ad9361/no_os_gpio.h"
#include <stdlib.h>
#include "ad9361/no_os_error.h"

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

/**
 * @brief Set the value of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @param value - The value.
 *                Example: NO_OS_GPIO_HIGH
 *                         NO_OS_GPIO_LOW
 * @return 0 in case of success, -1 otherwise.
 */
int32_t no_os_gpio_set_value(struct no_os_gpio_desc *desc,
			     uint8_t value)
{
  XGpioPs_WritePin(desc->Gpio, 54+desc->number, value);  
  return 0;
}



/* Obtain the GPIO decriptor. */
int32_t no_os_gpio_get(struct no_os_gpio_desc **desc,
		       const struct no_os_gpio_init_param *param)
               {

return 0;
                   
               }

/* Obtain optional GPIO descriptor. */
int32_t no_os_gpio_get_optional(struct no_os_gpio_desc **desc,
				const struct no_os_gpio_init_param *param)
                {

return 0;                    
                }


                /* Free the resources allocated by no_os_gpio_get(). */
int32_t no_os_gpio_remove(struct no_os_gpio_desc *desc)
{

return 0;
    
}



/* Enable the output direction of the specified GPIO. */
int32_t no_os_gpio_direction_output(struct no_os_gpio_desc *desc,
				    uint8_t value)
                    {
return 0;
                        
                    }