
/*
 * Copyright (c) 2017 KNK.
 * All rights reserved.


 * This file is part of the PKRV MODEM Project.
 *
 * Author: KNK <@.>
 *
 */

#ifndef MODEM_H_STATS_EXT
#define MODEM_H_STATS_EXT

#ifndef MODEM_H_
#define MODEM_H_

#include <stdio.h>
#include <stdint.h>
//#include <stdbool.h>
//#include <string.h>
//#include <stdlib.h>

//#define CMD_BUF_DEPTH 128

#pragma pack(push,1)
struct stat_cntrl_tx
{
	uint16_t cnt_pkt;
	uint16_t cnt_err_pkt;
	uint16_t last_pkt;
	uint32_t last_dat;


	float max_pow_cntrl;
	float min_pow_cntrl;
	float acc_pow_cntrl;

};
#pragma pack(pop)



#pragma pack(push,1)
struct stat_rx
{
	uint16_t cnt_pkt;
	uint16_t cnt_err_pkt;
	uint16_t last_pkt;

	float max_pow;
	float min_pow;
	float acc_pow;

};
#pragma pack(pop)



#pragma pack(push,1)
struct stat_modem
{
	float TempFPGA;
	float TempAD9361;
	float pps_pow_cntrl;
	float pps_pow;
	uint8_t gpio_status;
	struct stat_cntrl_tx status_cntrl_tx;
	struct stat_rx  status_rx;
};
#pragma pack(pop)


struct stat_modem* stat_modem_init();

#define CMD_PROTO_MAGIC 0x90
#define CMD_GET_STATS   0x20

#pragma pack(push, 1)
typedef struct {
    uint8_t head[2];      // 0: 0x90, 1: 0x20

    // HEAP (mem.c)
    uint32_t mem_used;
    uint32_t mem_max;
    uint32_t mem_err;

    // PBUF Headers (memp.c)
    uint32_t pbuf_used;
    uint32_t pbuf_max;
    uint32_t pbuf_err;

    // PBUF POOL (payloads)
    uint32_t pool_used;
    uint32_t pool_max;
    uint32_t pool_err;

    // Link drops
    uint32_t link_drop;
} stats_resp_t;
#pragma pack(pop)

#endif

























#endif // MODEM_H_
