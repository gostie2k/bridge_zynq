#include "ad9361/no_os_axi_io.h"


/* AXI IO Read data */
int32_t no_os_axi_io_read(uint32_t base, uint32_t offset, uint32_t *data)
{
 u32 tmp=Xil_In32(base+offset);
 *data=tmp;
 return 0;
}

/* AXI IO Write data */
int32_t no_os_axi_io_write(uint32_t base, uint32_t offset, uint32_t data)
{
 Xil_Out32(base+offset,data);
 return 0;
}
