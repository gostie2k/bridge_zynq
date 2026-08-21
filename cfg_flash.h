#ifndef CFG_FLASH_H_
#define CFG_FLASH_H_

#include "device_config.h"
#include "radio_params.h"

/* ===================================================================
 * Config storage in QSPI flash (unification, Phase 3).
 *
 * Goal: chosen role/IP/MAC/ID must survive reboot and
 * power loss. Config is kept in two copies (A/B) in a flash area
 * safely above the boot image (BOOT.BIN). Each copy is protected
 * by magic + version + seq counter + CRC32. Load picks the
 * valid copy with the larger seq; save writes to the NON-latest
 * copy - so a power cut during a write does not corrupt the other.
 * =================================================================== */

/* Init QSPI controller. Call once at startup.
 * return 0 - ok, !=0 - error (persistence unavailable). */
int cfg_flash_init(void);

/* Read the profile from flash. On success fills *net and *radio, returns 1.
 * Returns 0 if no valid copy exists (both empty/corrupt) -
 * then the caller keeps firmware defaults. */
int cfg_flash_load(struct device_config *net, struct radio_params *radio);

/* Save the profile to flash (sector erase + page program + verify).
 * Writes network + radio in one record. WARNING: blocks ~1 s (sector erase).
 * Call from main loop, not from a callback. return 0 - ok, !=0 - error. */
int cfg_flash_save(const struct device_config *net, const struct radio_params *radio);

/* Erase both copies (factory reset: next boot finds no valid
 * config and takes firmware defaults).
 * return 0 - ok, !=0 - error. */
int cfg_flash_erase(void);

#endif /* CFG_FLASH_H_ */
