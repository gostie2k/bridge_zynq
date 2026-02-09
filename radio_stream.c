#include "bridge_func.h"
#include "radio_stream.h"
#include "xil_printf.h"
#include "xllfifo.h"
#include <string.h>

extern struct netif port3_bridge;

#define PL_BLOCK_SIZE 188       // Размер блока для FPGA
#define BLOCK_HEADER_SIZE 1     // 1 байт на счетчик сегментов
#define BLOCK_PAYLOAD_SIZE (PL_BLOCK_SIZE - BLOCK_HEADER_SIZE) // 187 байт данных

#define MAGIC_H 0xFA
#define MAGIC_L 0xCE

#define RING_SIZE (PL_BLOCK_SIZE * 64)

extern XLlFifo fifo;

static uint8_t tx_ring[RING_SIZE];
static uint16_t tx_head = 0, tx_tail = 0, tx_count = 0;

static uint8_t rx_ring[RING_SIZE];
static uint16_t rx_head = 0, rx_tail = 0, rx_count = 0;

static uint16_t global_tx_pkt_counter = 0;
static uint8_t  global_tx_seq_counter = 0;

static u32 dbg_tx_pkts = 0;
static u32 dbg_tx_blocks = 0;
static u32 dbg_rx_blocks = 0;
static u32 dbg_rx_pkts = 0;
static u32 dbg_err_seq_gap = 0; // Потери сегментов (радио)
static u32 dbg_err_crc = 0;     // Ошибки данных

// RX Парсер
typedef enum {
    ST_MAGIC_H, ST_MAGIC_L,
    ST_ID_H,    ST_ID_L,
    ST_LEN_H,   ST_LEN_L,
    ST_DATA,
    ST_CRC0, ST_CRC1, ST_CRC2, ST_CRC3
} rx_state_t;

static rx_state_t rx_state = ST_MAGIC_H;
static uint16_t rx_pkt_len = 0;
static uint16_t rx_pkt_received = 0;
static uint8_t  rx_pkt_buffer[1600];
static uint32_t rx_calc_crc = 0;
static uint32_t rx_recv_crc = 0;
static uint8_t  rx_last_seq = 0; // Для проверки непрерывности сегментов

// --- CRC32 ---
static uint32_t calc_crc32_byte(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (int j = 0; j < 8; j++) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
        else         crc >>= 1;
    }
    return crc;
}

// Хелпер отправки в FPGA
static void send_block_to_pl(uint8_t *block) {
    if (XLlFifo_iTxVacancy(&fifo) >= (PL_BLOCK_SIZE / 4)) {
        u32 word;
        for (int i = 0; i < PL_BLOCK_SIZE; i += 4) {
            word =  (u32)block[i]        | ((u32)block[i+1] << 8) |
                   ((u32)block[i+2] << 16)| ((u32)block[i+3] << 24);
            XLlFifo_TxPutWord(&fifo, word);
        }
        XLlFifo_iTxSetLen(&fifo, PL_BLOCK_SIZE);
        dbg_tx_blocks++;
    }
}



//static void send_block_to_pl(uint8_t *block) {
//    // Получаем сколько есть свободного места
//    u32 vacancy = XLlFifo_iTxVacancy(&fifo);
//
//    // Проверяем, хватит ли места (нужно 47 слов)
//    if (vacancy >= (PL_BLOCK_SIZE / 4)) {
//        u32 word;
//        for (int i = 0; i < PL_BLOCK_SIZE; i += 4) {
//            word =  (u32)block[i]        | ((u32)block[i+1] << 8) |
//                   ((u32)block[i+2] << 16)| ((u32)block[i+3] << 24);
//            XLlFifo_TxPutWord(&fifo, word);
//        }
//        XLlFifo_iTxSetLen(&fifo, PL_BLOCK_SIZE);
//        dbg_tx_blocks++;
//    } else {
//        // [ДИАГНОСТИКА] Если FIFO полон - сообщаем об этом раз в секунду
//        static int err_cnt = 0;
//        err_cnt++;
//        if (err_cnt % 5000 == 0) {
//            xil_printf("!!! FIFO FULL (STUCK) !!! Vacancy=%u. PL is not reading!\r\n", vacancy);
//        }
//    }
//}


//static void send_block_to_pl(uint8_t *block) {
//    // ВРЕМЕННЫЙ ТЕСТ: Игнорируем переполнение FIFO
//    // Просто считаем, что отправили, даже если FIFO полон
//
//    if (XLlFifo_iTxVacancy(&fifo) >= (PL_BLOCK_SIZE / 4)) {
//        u32 word;
//        for (int i = 0; i < PL_BLOCK_SIZE; i += 4) {
//            word =  (u32)block[i]        | ((u32)block[i+1] << 8) |
//                   ((u32)block[i+2] << 16)| ((u32)block[i+3] << 24);
//            XLlFifo_TxPutWord(&fifo, word);
//        }
//        XLlFifo_iTxSetLen(&fifo, PL_BLOCK_SIZE);
//    }
//    else {
//        // FIFO полон!
//        // Обычно мы тут ничего не делаем, но для теста сбросим его,
//        // чтобы увидеть, что счетчики растут.
//        XLlFifo_Reset(&fifo);
//    }
//
//           dbg_tx_blocks++;
//           radio_input_block(block, &port3_bridge);
//}



// =========================================================
// ЧАСТЬ TX (ОТПРАВКА)
// =========================================================

static void tx_push(uint8_t b) {
    tx_ring[tx_head] = b;
    tx_head = (tx_head + 1) % RING_SIZE;
    tx_count++;
}

static void tx_process() {
    while (tx_count >= BLOCK_PAYLOAD_SIZE) {
        uint8_t block[PL_BLOCK_SIZE];

        block[0] = global_tx_seq_counter++;

        for (int i = 0; i < BLOCK_PAYLOAD_SIZE; i++) {
            block[i + 1] = tx_ring[tx_tail];
            tx_tail = (tx_tail + 1) % RING_SIZE;
        }
        tx_count -= BLOCK_PAYLOAD_SIZE;

        send_block_to_pl(block);
    }
}

err_t radio_linkoutput(struct netif *netif, struct pbuf *p) {
    if ((RING_SIZE - tx_count) < (p->tot_len + 10)) return ERR_MEM;

    dbg_tx_pkts++;

    uint32_t crc = 0xFFFFFFFF;
    struct pbuf *q;
    for(q = p; q != NULL; q = q->next) {
        uint8_t *data = (uint8_t*)q->payload;
        for(int i = 0; i < q->len; i++) {
            crc = calc_crc32_byte(crc, data[i]);
        }
    }
    crc = ~crc;

    tx_push(MAGIC_H);
    tx_push(MAGIC_L);
    // Packet ID
    tx_push((global_tx_pkt_counter >> 8) & 0xFF);
    tx_push(global_tx_pkt_counter & 0xFF);
    global_tx_pkt_counter++;
    // Length
    tx_push((p->tot_len >> 8) & 0xFF);
    tx_push((p->tot_len & 0xFF));

    for(q = p; q != NULL; q = q->next) {
        uint8_t *data = (uint8_t*)q->payload;
        for(int i = 0; i < q->len; i++) tx_push(data[i]);
    }

    tx_push((crc >> 24) & 0xFF);
    tx_push((crc >> 16) & 0xFF);
    tx_push((crc >> 8) & 0xFF);
    tx_push(crc & 0xFF);

    tx_process();
    return ERR_OK;
}

void radio_tx_periodic_flush(void) {
    if (tx_count > 0 && tx_count < BLOCK_PAYLOAD_SIZE) {
        while (tx_count < BLOCK_PAYLOAD_SIZE) tx_push(0x00);
        tx_process();
    }
}

// =========================================================
// ЧАСТЬ RX (ПРИЕМ)
// =========================================================

void radio_input_block(uint8_t *block_188, struct netif *inp_netif) {
    dbg_rx_blocks++;

    uint8_t seq = block_188[0];
    if ((uint8_t)(rx_last_seq + 1) != seq) {
        dbg_err_seq_gap++;
    }
    rx_last_seq = seq;

    for (int i = 0; i < BLOCK_PAYLOAD_SIZE; i++) {
        rx_ring[rx_head] = block_188[i + 1];
        rx_head = (rx_head + 1) % RING_SIZE;
        rx_count++;
    }

    while (rx_count > 0) {
        uint8_t b = rx_ring[rx_tail];

        if (rx_state == ST_DATA) {
             rx_calc_crc = calc_crc32_byte(rx_calc_crc, b);
        }

        switch (rx_state) {
            case ST_MAGIC_H:
                if (b == MAGIC_H) rx_state = ST_MAGIC_L;
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                break;

            case ST_MAGIC_L:
                if (b == MAGIC_L) rx_state = ST_ID_H;
                else rx_state = ST_MAGIC_H;
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                break;

            case ST_ID_H: // Packet ID High
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                rx_state = ST_ID_L;
                break;

            case ST_ID_L: // Packet ID Low
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                rx_state = ST_LEN_H;
                break;

            case ST_LEN_H:
                rx_pkt_len = (b << 8);
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                rx_state = ST_LEN_L;
                break;

            case ST_LEN_L:
                rx_pkt_len |= b;
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                if (rx_pkt_len > 1520 || rx_pkt_len == 0) {
                    rx_state = ST_MAGIC_H;
                } else {
                    rx_pkt_received = 0;
                    rx_calc_crc = 0xFFFFFFFF; // Init CRC
                    rx_state = ST_DATA;
                }
                break;

            case ST_DATA:
                rx_pkt_buffer[rx_pkt_received++] = b;
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                if (rx_pkt_received == rx_pkt_len) {
                    rx_calc_crc = ~rx_calc_crc; // Finalize CRC
                    rx_state = ST_CRC0;
                }
                break;

            case ST_CRC0: rx_recv_crc = ((uint32_t)b << 24); rx_state = ST_CRC1; rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--; break;
            case ST_CRC1: rx_recv_crc |= ((uint32_t)b << 16); rx_state = ST_CRC2; rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--; break;
            case ST_CRC2: rx_recv_crc |= ((uint32_t)b << 8);  rx_state = ST_CRC3; rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--; break;
            case ST_CRC3: rx_recv_crc |= b;
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;

                // ПРОВЕРКА CRC
                if (rx_recv_crc == rx_calc_crc) {
                    struct pbuf *p = pbuf_alloc(PBUF_RAW, rx_pkt_len, PBUF_POOL);
                    if (p) {
                        pbuf_take(p, rx_pkt_buffer, rx_pkt_len);
                        if (inp_netif->input(p, inp_netif) != ERR_OK) pbuf_free(p);
                        else dbg_rx_pkts++;
                    }
                } else {
                    dbg_err_crc++;
                }
                rx_state = ST_MAGIC_H;
                break;
        }
    }
}

// Вывод статистики
void radio_stream_print_stats(void) {
    xil_printf("TX: %u Pkt -> %u Blk | RX: %u Blk (Gap: %u) -> %u Pkt (CRC Err: %u)\r\n",
        dbg_tx_pkts, dbg_tx_blocks,
        dbg_rx_blocks, dbg_err_seq_gap,
        dbg_rx_pkts, dbg_err_crc);
}

void radio_stream_init(void) {
    tx_head = tx_tail = tx_count = 0;
    rx_head = rx_tail = rx_count = 0;
}
