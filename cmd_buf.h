/**
 * @file
 * Command channel buffer
 */

/*
 * Copyright (c) 2017 KNK.
 * All rights reserved.


 * This file is part of the PKRV MODEM Project.
 *
 * Author: KNK <@.>
 *
 */

#ifndef CMD_BUF_H_
#define CMD_BUF_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define CMD_BUF_DEPTH 256

#pragma pack(push,1)
struct cmd_pkt
{
	uint8_t head[29];
	uint8_t data[1518];
	uint8_t code[253];
	uint8_t pkt_size;
	uint16_t size;
	//uint8_t is_val;
	//uint8_t rx_is_val;
	//uint16_t tmp;
};
#pragma pack(pop)

typedef struct {
	struct cmd_pkt pkt[CMD_BUF_DEPTH];
	uint head;
	uint tail;
	uint count;
} cmd_buf;


typedef struct {
    struct cmd_pkt* pkt;
    bool is_data;
} cmd_out_buf;


struct cmd_pkt* zero_cmd_init();
cmd_buf* cmd_buf_init();
void cmd_push(cmd_buf** _buffer, uint8_t* _data, uint16_t _size);
cmd_out_buf* cmd_out_buf_init();
void cmd_buf_exch(cmd_buf* buf, cmd_out_buf* out);



#endif // CMD_BUF_H_
