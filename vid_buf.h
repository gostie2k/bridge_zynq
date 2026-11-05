/**
 * @file
 * Video channel buffer
 */

/*
 * Copyright (c) 2017 KNK.
 * All rights reserved.


 * This file is part of the PKRV MODEM Project.
 *
 * Author: KNK <@.>
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define VID_BUF_DEPTH 1024
#define VID_PKT_MTU 1269

#pragma pack(push,1)
struct vid_pkt
{
    uint8_t head[159];
    uint8_t tel[1518];
    uint8_t vid_0[1212];
    uint8_t vid_1[1212];
    uint8_t vid_2[1212];
    uint8_t vid_3[1212];
    uint8_t vid_4[1212];
    uint8_t code[1263];


   //uint8_t head[102];
   //uint8_t tel[1008];
   //uint8_t vid_0[1269];
   //uint8_t vid_1[1269];
   //uint8_t vid_2[1269];
   //uint8_t vid_3[1269];
   //uint8_t vid_4[1269];
   //uint8_t anal[62];
   //uint8_t code[1483];

	uint8_t gr_size;
	uint16_t gr_cnt;
	uint8_t tel_is_val;
	uint8_t vid_0_is_val;
	uint8_t vid_1_is_val;
	uint8_t vid_2_is_val;
	uint8_t vid_3_is_val;
	uint8_t vid_4_is_val;
	uint8_t anal_is_val;
	uint8_t rx_is_val;
	uint8_t tmp;

    uint16_t vid_0_size;
    uint16_t vid_1_size;
    uint16_t vid_2_size;
    uint16_t vid_3_size;
    uint16_t vid_4_size;
};
#pragma pack(pop)

typedef struct {
	struct vid_pkt pkt[VID_BUF_DEPTH];
	uint head;
	uint tail;
	uint count;
	struct vid_pkt current;
	int current_index;
} vid_buf;

typedef struct {
	struct vid_pkt* pkt;
    bool is_data;
} vid_out_buf;


struct vid_pkt* zero_vid_init()
{
	struct vid_pkt* pkt;
	pkt = malloc(sizeof(struct vid_pkt));
	memset(pkt, 0x00, sizeof(struct vid_pkt));
	uint8_t sync[6] = { 0xb6, 0xce, 0x26, 0x1d, 0x6d, 0x9c };
    for (unsigned j = 0; j < 6; ++j) {
	   memcpy((uint8_t*)  &pkt->head[0] + j   ,   &sync[0] + j, 1);
				    }
	return pkt;
}


vid_buf* vid_buf_init() {
	vid_buf* buf;
	buf = malloc(sizeof(vid_buf));
	uint8_t sync[6] = { 0xb6, 0xce, 0x26, 0x1d, 0x6d, 0x9c };
	for (unsigned i=0; i<VID_BUF_DEPTH; i++)
	{
		    for (unsigned j = 0; j < 6; ++j) {
		        memcpy((uint8_t*)  &buf->pkt[i].head[0] + j   ,   &sync[0] + j, 1);
		    }
	}
	 for (unsigned j = 0; j < 6; ++j) {
			        memcpy((uint8_t*)  &buf->current.head[0] + j   ,   &sync[0] + j, 1);
    }
	buf->count = 0;
	buf->head = 0;
	buf->tail = 0;
	buf->current_index = 0;
	return buf;
}

void vid_push(vid_buf** _buffer, uint8_t* _data, uint16_t _size) {
	if (((*_buffer)->count >= VID_BUF_DEPTH)||(_size >= VID_PKT_MTU)) {
		return;
	}

	uint8_t check = 0;
	for (int k=0; k<_size; k++) check+=_data[k];

	switch((*_buffer)->current_index) {
	case 0:
		(*_buffer)->current.vid_0_size = _size;
		memcpy((*_buffer)->current.vid_0, _data, _size);
		memcpy((*_buffer)->current.head + 80, &_size  ,sizeof(uint16_t) );
		memcpy((*_buffer)->current.head + 90, &check  ,sizeof(uint8_t) );
		break;
	case 1:
		(*_buffer)->current.vid_1_size = _size;
		memcpy((*_buffer)->current.vid_1, _data, _size);
		memcpy((*_buffer)->current.head + 82, &_size  ,sizeof(uint16_t) );
		memcpy((*_buffer)->current.head + 91, &check  ,sizeof(uint8_t) );
		break;
	case 2:
		(*_buffer)->current.vid_2_size = _size;
		memcpy((*_buffer)->current.vid_2, _data, _size);
		memcpy((*_buffer)->current.head + 84, &_size  ,sizeof(uint16_t) );
		memcpy((*_buffer)->current.head + 92, &check  ,sizeof(uint8_t) );
		break;
	case 3:
		(*_buffer)->current.vid_3_size = _size;
		memcpy((*_buffer)->current.vid_3, _data, _size);
		memcpy((*_buffer)->current.head + 86, &_size  ,sizeof(uint16_t) );
		memcpy((*_buffer)->current.head + 93, &check  ,sizeof(uint8_t) );
		break;
	case 4:
		(*_buffer)->current.vid_4_size = _size;
		memcpy((*_buffer)->current.vid_4, _data, _size);
		memcpy((*_buffer)->current.head + 88, &_size  ,sizeof(uint16_t) );
		memcpy((*_buffer)->current.head + 94, &check  ,sizeof(uint8_t) );
		break;
	}

	(*_buffer)->current_index++;
	if ((*_buffer)->current_index == 5) {
		(*_buffer)->current_index = 0;
		(*_buffer)->pkt[(*_buffer)->head] = (*_buffer)->current;
		(*_buffer)->head++;
		(*_buffer)->count++;
		if ((*_buffer)->head == VID_BUF_DEPTH) {
			(*_buffer)->head = 0;
		}
	}
}


vid_out_buf* vid_out_buf_init()
{
    vid_out_buf* buffer = (vid_out_buf*)malloc(sizeof(vid_out_buf));
    if (!buffer) { return NULL; }
    buffer->pkt     = (struct vid_pkt*)malloc(sizeof(struct vid_pkt));
    buffer->is_data = false;
    return buffer;
}


void vid_buf_exch(vid_buf* buf, vid_out_buf* out) {

	 if ((buf->count == 0)||(out->is_data)) return;
	 else
	 {
		 *out->pkt = buf->pkt[buf->tail];
		 buf->tail++;
		 buf->count--;
		 if (buf->tail == VID_BUF_DEPTH) {
		 	buf->tail = 0;
		 }
		 out->is_data = true;
	 }
}


