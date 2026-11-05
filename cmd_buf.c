#include "cmd_buf.h"

struct cmd_pkt* zero_cmd_init()
{
	struct cmd_pkt* pkt;
	pkt = malloc(sizeof(struct cmd_pkt));
	memset(pkt, 0x00, sizeof(struct cmd_pkt));
	uint8_t sync[6] = { 0xb6, 0xce, 0x26, 0x1d, 0x6d, 0x9c };
	uint8_t tmp[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    for (unsigned j = 0; j < 6; ++j) {
	   memcpy((uint8_t*)  &pkt->head[0] + j   ,   &sync[0] + j, 1);
	   memcpy((uint8_t*)  &pkt->code[252]-5 + j   ,   &tmp[0] + j, 1);
				    }
	return pkt;
}


cmd_buf* cmd_buf_init() {
	cmd_buf* buf;
	buf = malloc(sizeof(cmd_buf));
	uint8_t sync[6] = { 0xb6, 0xce, 0x26, 0x1d, 0x6d, 0x9c };
	uint8_t tmp[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	for (unsigned i=0; i<CMD_BUF_DEPTH; i++){
		    for (unsigned j = 0; j < 6; ++j) {
		        memcpy((uint8_t*)  &buf->pkt[i].head[0] + j, &sync[0] + j, 1);
		        memcpy((uint8_t*)  &buf->pkt[i].code[252]-5 + j   ,   &tmp[0] + j, 1);
		    }
	}
	buf->count = 0;
	buf->head = 0;
	buf->tail = 0;
	return buf;
}


void cmd_push(cmd_buf** _buffer, uint8_t* _data, uint16_t _size) {

	if ((*_buffer)->count >= CMD_BUF_DEPTH) {
		return;
	}

   (*_buffer)->pkt[(*_buffer)->head].size = _size;
   uint8_t check = 0;
   for (int k=0; k<_size; k++) check+=_data[k];
   memcpy((*_buffer)->pkt[(*_buffer)->head].data, _data , _size);
   memcpy((*_buffer)->pkt[(*_buffer)->head].head+20, &_size  ,sizeof(uint16_t) );
   memcpy((*_buffer)->pkt[(*_buffer)->head].head+22, &check  ,sizeof(uint8_t) );

  // xil_printf("CMD_PUSH -> len = %d, check = %d \n\r", _size, check);

	(*_buffer)->head++;
	(*_buffer)->count++;

	if ((*_buffer)->head == CMD_BUF_DEPTH) {
			(*_buffer)->head = 0;
		}
}

cmd_out_buf* cmd_out_buf_init()
{
    cmd_out_buf* buffer = (cmd_out_buf*)malloc(sizeof(cmd_out_buf));
    if (!buffer) { return NULL; }
    buffer->pkt     = (struct cmd_pkt*) malloc(sizeof(struct cmd_pkt));
    buffer->is_data = false;
    return buffer;
}


void cmd_buf_exch(cmd_buf* buf, cmd_out_buf* out) {

	 if ((buf->count == 0)||(out->is_data)) return;
	 else{
		 *out->pkt = buf->pkt[buf->tail];
		 buf->tail++;
		 buf->count--;
		 if (buf->tail == CMD_BUF_DEPTH) {
		 	buf->tail = 0;
		 }
		 out->is_data = true;
	 }
}


