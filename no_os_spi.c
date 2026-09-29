
#include <inttypes.h>
#include "ad9361/no_os_spi.h"
#include <stdlib.h>
#include "ad9361/no_os_error.h"
//#include "no_os_mutex.h"
//#include "no_os_alloc.h"

#include "spi_engine/spi_engine.h"
extern struct no_os_spi_desc* spi_eng_ad9361;

/*
*	SPI Comm Helpers
*/
#define AD_READ		(0 << 15)
#define AD_WRITE		(1 << 15)
#define AD_CNT(x)	((((x) - 1) & 0x7) << 12)
#define AD_ADDR(x)	((x) & 0x3FF)
#define AD_ADDR_7FF(x)	((x) & 0x7FF)

#define AD_RW(x)	((x>>15) & 0x0001)
#define AD_NUM(x)	((x>>12) & 0x0007)+1






/**
 * @brief Write and read data to/from SPI.
 * @param desc - The SPI descriptor.
 * @param data - The buffer with the transmitted/received data.
 * @param bytes_number - Number of bytes to write/read.
 * @return 0 in case of success, -1 otherwise.
 */
int32_t no_os_spi_write_and_read(struct no_os_spi_desc *desc,
				 uint8_t *data,
				 uint16_t bytes_number)
{
   
   int32_t ret=0;
   uint16_t cmd;
   u32 buf=0;
   u32 RecvChar;
   u32 b=0;
   u32 tmp;
   
   
   cmd=data[0];
   cmd<<=8;
   cmd+=data[1];

   bool is_wd=(AD_RW(cmd)==0x0001); 
   uint16_t adr=AD_ADDR(cmd);
   uint16_t num=AD_NUM(cmd);   
   
//dev_dbg(desc, "%s: reg 0x%"PRIX32" val 0x%X", __func__, num, data[0]);
//dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X", __func__, reg, buf[2]);
 //printf('%x',data[2]);


  return spi_engine_write_and_read(spi_eng_ad9361, data, bytes_number);


     for (uint16_t k=0;k<num;k++)
         {  
             buf=0;
             cmd = (is_wd)?AD_WRITE:AD_READ;
             cmd|=AD_ADDR(adr-k);                   
             cmd|=AD_CNT(1);
              
             buf=cmd;
             buf<<=8;  
             tmp=data[2+k];
             buf+=tmp; 
             
             for (int i=0;i<24;i++) XGpioPs_WritePin(desc->Gpio, 54+i, (buf>>i));

             //XGpioPs_Write(desc->Gpio, 2, 0x82ffffff&(buf|0x82000000));

             XGpioPs_WritePin(desc->Gpio, 54+24, 1); 
             XGpioPs_WritePin(desc->Gpio, 54+24, 0); 


       if (!is_wd)
         {    

              b=0;
              RecvChar=0;

              while(0x00000000==(XGpioPs_ReadPin(desc->Gpio, 54+24+32)&0x00000001))
             {
                 RecvChar++;
                 if (RecvChar==0x00ffffff)  
                {
                  b=0xff;
                  return  1;
                }

            }
              
              
              for (int i=0;i<8;i++)
              {
               RecvChar=XGpioPs_ReadPin(desc->Gpio, 54+7-i+32)&0x00000001;
               b+=RecvChar;
               if (i<7) b<<=1;

               //RecvChar = XGpioPs_Read(desc->Gpio, 3);
               //b = RecvChar&0x000000ff;


              }
         
          
             data[2+k]=b;
              
            
             XGpioPs_WritePin(desc->Gpio, 54+25, 0);   
             RecvChar=0;
             while(0x00000001==(XGpioPs_ReadPin(desc->Gpio, 54+24+32)&0x00000001))
             {
                 RecvChar++;
                 if (RecvChar==0x00ffffff)  
                {
                  b=0xff;
                  return  1;
                }
             }

             XGpioPs_WritePin(desc->Gpio, 54+25, 1); 
             

}




             

         } 
       
   









   


	//if (!desc || !desc->platform_ops)
	//	return -EINVAL;

	//if (!desc->platform_ops->write_and_read)
	//	return -ENOSYS;

	//no_os_mutex_lock(desc->bus->mutex);
	//ret =  desc->platform_ops->write_and_read(desc, data, bytes_number);
	//no_os_mutex_unlock(desc->bus->mutex);








	return ret;

}





/**
 * @brief Write and read data to/from SPI.
 * @param desc - The SPI descriptor.
 * @param data - The buffer with the transmitted/received data.
 * @param bytes_number - Number of bytes to write/read.
 * @return 0 in case of success, -1 otherwise.
 */
inline int32_t no_os_spi_write_and_read_11bit(struct no_os_spi_desc *desc,
				 uint8_t *data,
				 uint16_t bytes_number)
{
   int32_t ret=0;
   uint16_t cmd;
   
   cmd=data[0];
   cmd<<=8;
   cmd+=data[1];

   bool is_wd=(AD_RW(cmd)==0x0001); 
   uint16_t adr=AD_ADDR_7FF(cmd);
   //uint16_t num=AD_NUM(cmd);

   uint16_t num=bytes_number;
   
//dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X", __func__, reg, buf[2]);
//dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X", __func__, 0, data[0]);
//dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X", __func__, 1, data[1]);

   //printf("\n\r SPI = ");

     for (uint16_t k=0;k<num;k++)
         {  
             u32 buf=0;  
             cmd = (is_wd)?AD_WRITE:AD_READ;
             cmd|=AD_ADDR_7FF(adr-k);
             //cmd|=AD_CNT(1);


            // printf(" 0x%X", cmd);



              
             buf=cmd;
             buf<<=8;  
             u32 tmp=data[2+k]; 
             buf+=tmp; 
             

              XGpioPs_Write(desc->Gpio, 2, 0x82ffffff&(buf|0x82000000));


             //for (int i=0;i<24;i++) XGpioPs_WritePin(desc->Gpio, 54+i, (buf>>i));





             XGpioPs_WritePin(desc->Gpio, 54+24, 1); 
             XGpioPs_WritePin(desc->Gpio, 54+24, 0); 


       if (!is_wd)
         {    
              u32 RecvChar;
              u32 b=0;  

              RecvChar=0;
              while(0x00000000==(XGpioPs_ReadPin(desc->Gpio, 54+24+32)&0x00000001))
             {
                 RecvChar++;
                 if (RecvChar==0x00ffffff)  
                {
                  b=0xff;
                  return  1;
                }

            }
              
              
              for (int i=0;i<8;i++)
              {
               //RecvChar=XGpioPs_ReadPin(desc->Gpio, 54+7-i+32)&0x00000001;
               //b+=RecvChar;
               //if (i<7) b<<=1;

               RecvChar = XGpioPs_Read(desc->Gpio, 3);
               b = RecvChar&0x000000ff;

              }
         
          
             data[2+k]=b;
              
            
             XGpioPs_WritePin(desc->Gpio, 54+25, 0);   
             RecvChar=0;
             while(0x00000001==(XGpioPs_ReadPin(desc->Gpio, 54+24+32)&0x00000001))
             {
                 RecvChar++;
                 if (RecvChar==0x00ffffff)  
                {
                  b=0xff;
                  return  1;
                }
             }

             XGpioPs_WritePin(desc->Gpio, 54+25, 1); 
             

}




             

         } 
       
   









   


	//if (!desc || !desc->platform_ops)
	//	return -EINVAL;

	//if (!desc->platform_ops->write_and_read)
	//	return -ENOSYS;

	//no_os_mutex_lock(desc->bus->mutex);
	//ret =  desc->platform_ops->write_and_read(desc, data, bytes_number);
	//no_os_mutex_unlock(desc->bus->mutex);








	return ret;

}




/* Initialize the SPI communication peripheral. */
int32_t no_os_spi_init(struct no_os_spi_desc **desc,
		       const struct no_os_spi_init_param *param)
               {
    
                  //struct no_os_spi_desc *tmp;
                  //tmp=*desc;

                  //*desc=param->parent;

                  *desc=param->parent;

                  //tmp->Gpio=param->Gpio;
                    

                   return 0;
               }

               
               
               
int32_t no_os_spi_remove(struct no_os_spi_desc *desc)
{

return 0;    
}
