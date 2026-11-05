#ifndef CFG_FLASH_H_
#define CFG_FLASH_H_

#include "device_config.h"
#include "radio_params.h"

/* Config in QSPI flash, two copies (A/B) with magic + version + seq + CRC32.
 * Load takes the valid copy with the bigger seq, save writes the other one,
 * so a power cut cannot kill both. */

int cfg_flash_init(void);                                    /* 0 = ok */
int cfg_flash_load(struct device_config *net, struct radio_params *radio);  /* 1 = loaded */
int cfg_flash_save(const struct device_config *net, const struct radio_params *radio);
int cfg_flash_erase(void);

#endif /* CFG_FLASH_H_ */
