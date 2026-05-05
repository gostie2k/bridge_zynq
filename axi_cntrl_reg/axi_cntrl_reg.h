
#ifndef AXI_CNTRL_REG_H
#define AXI_CNTRL_REG_H


/****************** Include Files ********************/
#include "xil_types.h"
#include "xstatus.h"
#include "xil_io.h"
//#include "../platform_config.h"

//void write_creg(u32 Offset, u32 Data);
//u32 read_creg(u32 Offset);

/**************************** Type Definitions *****************************/
/**
 *
 * Write a value to a AXI_CNTRL_REG register. A 32 bit write is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is written.
 *
 * @param   BaseAddress is the base address of the AXI_CNTRL_REGdevice.
 * @param   RegOffset is the register offset from the base to write to.
 * @param   Data is the data written to the register.
 *
 * @return  None.
 *
 * @note
 * C-style signature:
 * 	void AXI_CNTRL_REG_mWriteReg(u32 BaseAddress, unsigned RegOffset, u32 Data)
 *
 */
#define AXI_CNTRL_REG_WRITE(BaseAddress, RegOffset, Data) \
  	Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))

/**
 *
 * Read a value from a AXI_CNTRL_REG register. A 32 bit read is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is read from the register. The most significant data
 * will be read as 0.
 *
 * @param   BaseAddress is the base address of the AXI_CNTRL_REG device.
 * @param   RegOffset is the register offset from the base to write to.
 *
 * @return  Data is the data from the register.
 *
 * @note
 * C-style signature:
 * 	u32 AXI_CNTRL_REG_mReadReg(u32 BaseAddress, unsigned RegOffset)
 *
 */
#define AXI_CNTRL_REG_READ(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))



#endif // AXI_CNTRL_REG_H
