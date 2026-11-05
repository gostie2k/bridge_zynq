#include <limits.h>

#ifndef ERROR_TRACKER_H_
#define ERROR_TRACKER_H_

extern  unsigned long val_recv_eth;
extern  unsigned long val_send_fifo;
extern  unsigned long val_recv_fifo;
extern  unsigned long val_send_eth;
extern  unsigned long val_buff_write;
extern  unsigned long val_buff_read;
extern  unsigned long val_group_buf;

extern unsigned long val_bytes_send;
extern unsigned long val_bytes_read;

unsigned long err_buff_count();
void err_reset();
void err_inc_recv_eth();
void err_inc_send_fifo();
void err_inc_recv_fifo();
void err_inc_recv_fifo_wrong();
void err_inc_send_eth();
void err_buff_write();
void err_group_buff_created();
void err_check();
unsigned long err_last_packet();

unsigned long err_get_recv_eth();
unsigned long err_get_send_fifo();
unsigned long err_get_recv_fifo();
unsigned long err_get_send_eth();
unsigned long err_get_buff_write();
unsigned long err_get_buff_read();
unsigned long err_get_val_gr_buf();


#endif // ERROR_TRACKER_H_
