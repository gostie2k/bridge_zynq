#include "error_tracker.h"

unsigned long val_recv_eth;
unsigned long val_send_fifo;
unsigned long val_recv_fifo;
unsigned long val_recv_fifo_wrong;
unsigned long val_send_eth;
unsigned long val_buff_write;
unsigned long val_buff_read;
unsigned long val_group_buf;

unsigned long val_recv_eth   = 0;
unsigned long val_send_fifo  = 0;
unsigned long val_recv_fifo  = 0;
unsigned long val_recv_fifo_wrong = 0;
unsigned long val_send_eth   = 0;
unsigned long val_buff_write = 0;
unsigned long val_buff_read  = 0;
unsigned long val_group_buf  = 0;
unsigned long val_err_count  = 0;

unsigned long val_bytes_send = 0;
unsigned long val_bytes_read = 0;

unsigned long err_buff_count(){
	return val_err_count;
}

void err_reset() {
	val_recv_eth	= 0;	// Получено через ethernet
	val_send_fifo	= 0; 	// Отправлено в FIFO
	val_recv_fifo	= 0;	// Получено из FIFO
	val_send_eth	= 0;	// Отправлено через ethernet
	val_buff_write  = 0;	// Запись в буфер
	val_group_buf   = 0;	// Формирование группового буфера
	val_err_count++;
}

void err_inc_recv_eth() {
	val_recv_eth++;

	if (val_recv_eth > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_inc_send_fifo() {
	val_send_fifo++;

	if (val_send_fifo > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_inc_recv_fifo() {
	val_recv_fifo++;

	if (val_recv_fifo > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_inc_recv_fifo_wrong() {
	val_recv_fifo_wrong++;

	if (val_recv_fifo_wrong > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_inc_send_eth() {
	val_send_eth++;

	if (val_send_eth > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_buff_read() {
	val_buff_read++;

	if (val_buff_read > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_buff_write() {
	val_buff_write++;

	if (val_buff_write > (ULONG_MAX - 15)) {
		err_reset();
	}
}

unsigned long err_last_packet() {
	return val_recv_eth % 5;
}

void err_group_buff_created() {

	val_group_buf++;

	if (val_group_buf > (ULONG_MAX - 15)) {
		err_reset();
	}
}

void err_check() {
	if (1) {

	}
}

unsigned long err_get_recv_eth() { return val_recv_eth; }
unsigned long err_get_send_fifo() { return val_send_fifo; }
unsigned long err_get_recv_fifo() { return val_recv_fifo; }
unsigned long err_get_send_eth() { return val_send_eth; }
unsigned long err_get_buff_write() { return val_buff_write; }
unsigned long err_get_buff_read() { return val_buff_read; }
unsigned long err_get_val_gr_buf() { return val_group_buf; }
