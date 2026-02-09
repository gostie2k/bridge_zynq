#include "ad9361/no_os_delay.h"
#include "sleep.h"


/* Generate microseconds delay. */
void no_os_udelay(uint32_t usecs)
{

    usleep(usecs);

}

/* Generate miliseconds delay. */
void no_os_mdelay(uint32_t msecs)
{
    usleep(msecs*1000);
}

/* Get current time */
struct no_os_time no_os_get_time(void)
{
struct no_os_time time;
time.s=0;
time.us=0;
return time;

}
