

#ifndef _NO_OS_SPI_H_
#define _NO_OS_SPI_H_

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include "xgpiops.h"

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

#define	NO_OS_SPI_CPHA	0x01
#define	NO_OS_SPI_CPOL	0x02
#define SPI_MAX_BUS_NUMBER 8

/******************************************************************************/
/*************************** Types Declarations *******************************/
/******************************************************************************/
/**
 * @enum no_os_spi_mode
 * @brief SPI configuration for clock phase and polarity.
 */
enum no_os_spi_mode {
	/** Data on rising, shift out on falling */
	NO_OS_SPI_MODE_0 = (0 | 0),
	/** Data on falling, shift out on rising */
	NO_OS_SPI_MODE_1 = (0 | NO_OS_SPI_CPHA),
	/** Data on rising, shift out on falling */
	NO_OS_SPI_MODE_2 = (NO_OS_SPI_CPOL | 0),
	/** Data on falling, shift out on rising */
	NO_OS_SPI_MODE_3 = (NO_OS_SPI_CPOL | NO_OS_SPI_CPHA)
};

/**
 * @enum no_os_spi_bit_order
 * @brief SPI configuration for bit order (MSB/LSB).
 */
enum no_os_spi_bit_order {
	/** Most-significant bit (MSB) first */
	NO_OS_SPI_BIT_ORDER_MSB_FIRST = 0,
	/** Least-significant bit (LSB) first */
	NO_OS_SPI_BIT_ORDER_LSB_FIRST = 1,
};

/**
 * @struct no_os_spi_msg_list
 * @brief List item describing a SPI transfer
 */
struct no_os_spi_msg {
	/** Buffer with data to send. If NULL, 0x00 will be sent */
	uint8_t			*tx_buff;
	/** Buffer where to store data. If NULL, incoming data won't be saved */
	uint8_t			*rx_buff;
	/** Length of buffers. Must have equal size. */
	uint32_t		bytes_number;
	/** If set, CS will be deasserted after the transfer */
	uint8_t			cs_change;
	/**
	 * Minimum delay (in us) between the CS de-assert event of the current message
	 * and the assert of the next one.
	 */
	uint32_t		cs_change_delay;
	/** Delay (in us) between the CS assert and the first SCLK edge. */
	uint32_t		cs_delay_first;
	/** Delay (in us) between the last SCLK edge and the CS deassert */
	uint32_t		cs_delay_last;
};


/**
 * @struct no_os_platform_spi_delays
 * @brief Delays resulted from components in the SPI signal path. The values is ns.
 */
struct no_os_platform_spi_delays {
	uint32_t cs_delay_first;
	uint32_t cs_delay_last;
};

/**
 * @struct no_os_spi_platform_ops
 * @brief Structure holding SPI function pointers that point to the platform
 * specific function
 */
struct no_os_spi_platform_ops ;


/**
 * @struct no_os_spi_init_param
 * @brief Structure holding the parameters for SPI initialization
 */
struct no_os_spi_init_param {
	/** Device ID */
	uint32_t	device_id;
	/** maximum transfer speed */
	uint32_t	max_speed_hz;
	/** SPI chip select */
	uint8_t		chip_select;
	/** SPI mode */
	enum no_os_spi_mode	mode;
	/** SPI bit order */
	enum no_os_spi_bit_order	bit_order;
	const struct no_os_spi_platform_ops *platform_ops;
	struct no_os_platform_spi_delays platform_delays;
	/**  SPI extra parameters (device specific) */
	void		*extra;
	/** Parent of the device */
	struct no_os_spi_desc *parent;

    XGpioPs *Gpio;

};



/**
 * @struct no_os_spibus_desc
 * @brief SPI bus descriptor
*/
struct no_os_spibus_desc {
	/** SPI bus mutex (lock) */
	void 		*mutex;
	/** SPI bus slave number*/
	uint8_t         slave_number;
	/** SPI bus device id */
	uint32_t	device_id;
	/** SPI bus max speed */
	uint32_t	max_speed_hz;
	/** SPI bus mode */
	enum no_os_spi_mode	mode;
	/** SPI bus bit order */
	enum no_os_spi_bit_order	bit_order;
	/** SPI bus platform ops */
	const struct no_os_spi_platform_ops *platform_ops;
	/** SPI bus extra */
	void		*extra;
};


/**
 * @struct no_os_spi_desc
 * @brief Structure holding SPI descriptor.
 */
struct no_os_spi_desc {
	/** SPI bus address */
	struct no_os_spibus_desc	*bus;
	/** SPI bus number (0 for SPI0, 1 for SPI1, ...) */
	uint32_t	device_id;
	/** maximum transfer speed */
	uint32_t	max_speed_hz;
	/** SPI chip select */
	uint8_t		chip_select;
	/** SPI mode */
	enum no_os_spi_mode	mode;
	/** SPI bit order */
	enum no_os_spi_bit_order	bit_order;
	const struct no_os_spi_platform_ops *platform_ops;
	struct no_os_platform_spi_delays platform_delays;
	/**  SPI extra parameters (device specific) */
	void		*extra;
	/** Parent of the device */
	struct no_os_spi_desc *parent;

    XGpioPs *Gpio;
};

/**
 * @struct no_os_spi_platform_ops
 * @brief Structure holding SPI function pointers that point to the platform
 * specific function
 */
struct no_os_spi_platform_ops {
	/** SPI initialization function pointer */
	int32_t (*init)(struct no_os_spi_desc **, const struct no_os_spi_init_param *);
	/** SPI write/read function pointer */
	int32_t (*write_and_read)(struct no_os_spi_desc *, uint8_t *, uint16_t);
	/** Iterate over the spi_msg array and send all messages at once */
	int32_t (*transfer)(struct no_os_spi_desc *, struct no_os_spi_msg *, uint32_t);
	/** Iterate over the spi_msg array and send all messages using DMA.
	 * Blocks until the transfer is completed.
	 */
	int32_t (*dma_transfer_sync)(struct no_os_spi_desc *, struct no_os_spi_msg *,
				     uint32_t);
	/** Iterate over the spi_msg array and send all messages using DMA.
	 * Returns immediately after the transfer is started and invokes a
	 * callback once all the messages have been transfered.
	 */
	int32_t (*dma_transfer_async)(struct no_os_spi_desc *, struct no_os_spi_msg *,
				      uint32_t, void (*)(void *), void *);
	/** SPI remove function pointer */
	int32_t (*remove)(struct no_os_spi_desc *);
};







/******************************************************************************/
/************************ Functions Declarations ******************************/
/******************************************************************************/


/* Write and read data to/from SPI. */
int32_t no_os_spi_write_and_read(struct no_os_spi_desc *desc,
				 uint8_t *data,
				 uint16_t bytes_number);




int32_t no_os_spi_write_and_read_11bit(struct no_os_spi_desc *desc,
				 uint8_t *data,
				 uint16_t bytes_number);
                 

         
/* Initialize the SPI communication peripheral. */
int32_t no_os_spi_init(struct no_os_spi_desc **desc,
		       const struct no_os_spi_init_param *param);

/* Free the resources allocated by no_os_spi_init(). */
int32_t no_os_spi_remove(struct no_os_spi_desc *desc);


#endif // _NO_OS_SPI_H_
