#include "bridge_func.h"
#include "radio_stream.h"
#include "xil_printf.h"
#include "xllfifo.h"
#include <string.h>
#include <stdbool.h>
#include "modem.h"
#include "watchdog.h"
#include "cmd_udp.h"   /* для g_log_level / LOG_HOT */

extern struct netif port3_bridge;

#define PL_BLOCK_SIZE 188       // Р Р°Р·РјРµСЂ Р±Р»РѕРєР° РґР»СЏ FPGA
#define BLOCK_HEADER_SIZE 1     // 1 Р±Р°Р№С‚ РЅР° СЃС‡РµС‚С‡РёРє СЃРµРіРјРµРЅС‚РѕРІ
#define BLOCK_PAYLOAD_SIZE (PL_BLOCK_SIZE - BLOCK_HEADER_SIZE) // 187 Р±Р°Р№С‚ РґР°РЅРЅС‹С…

#define MAGIC_H 0xFA
#define MAGIC_L 0xCE

//#define RING_SIZE (PL_BLOCK_SIZE * 512)

#define RING_SIZE (PL_BLOCK_SIZE * 1024)

extern XLlFifo fifo;

static uint8_t tx_ring[RING_SIZE];
static uint32_t tx_head = 0, tx_tail = 0, tx_count = 0;

static uint8_t rx_ring[RING_SIZE];
static uint32_t rx_head = 0, rx_tail = 0, rx_count = 0;

static uint16_t global_tx_pkt_counter = 0;
static uint8_t  global_tx_seq_counter = 0;

static int  loopback_enabled = 0;  // 0 = СЂРµР°Р»СЊРЅС‹Р№ FPGA, 1 = РїСЂРѕРіСЂР°РјРјРЅС‹Р№ loopback


#define PL_RX_WORDS  47
#define PL_RX_BYTES  (PL_RX_WORDS * 4)

static u32     drain_rx_words[PL_RX_WORDS];
static uint8_t drain_rx_block[PL_RX_BYTES];

uint16_t cnt_tx_seg=0;




static u32 dbg_tx_pkts = 0;
static u32 dbg_tx_blocks = 0;
static u32 dbg_rx_blocks = 0;
static u32 dbg_rx_pkts = 0;
static u32 dbg_err_seq_gap = 0;   // РџРѕС‚РµСЂРё СЃРµРіРјРµРЅС‚РѕРІ (СЂР°РґРёРѕ)
static u32 dbg_err_crc = 0;       // РћС€РёР±РєРё CRC
static u32 dbg_err_fifo_full = 0; // FIFO РїРѕР»РѕРЅ вЂ” Р±Р»РѕРє РґСЂРѕРїРЅСѓС‚
static u32 dbg_err_rx_overflow = 0; // rx_ring РїРµСЂРµРїРѕР»РЅРµРЅ
static u32 dbg_err_pkt_gap = 0;   // РџСЂРѕРїСѓСЃРєРё packet ID РЅР° RX

static int crc_dump_done = 0;     // Р¤Р»Р°Рі РґР»СЏ РѕРґРЅРѕРєСЂР°С‚РЅРѕРіРѕ РґР°РјРїР° CRC-РѕС€РёР±РєРё
static u32 dbg_err_tx_stall = 0;  // tx_process РїСЂРµСЂРІР°Р»СЃСЏ (TX FIFO РїРѕР»РѕРЅ)
static u32 dbg_err_tx_reject = 0; // РџР°РєРµС‚ РѕС‚Р±СЂРѕС€РµРЅ (tx_ring РїРѕР»РѕРЅ)

// RX РџР°СЂСЃРµСЂ
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
static uint8_t  rx_last_seq = 0;      // Р”Р»СЏ РїСЂРѕРІРµСЂРєРё РЅРµРїСЂРµСЂС‹РІРЅРѕСЃС‚Рё СЃРµРіРјРµРЅС‚РѕРІ
static int      rx_first_block_seen = 0;
static uint16_t rx_last_pkt_id = 0;  // Р”Р»СЏ РїСЂРѕРІРµСЂРєРё РЅРµРїСЂРµСЂС‹РІРЅРѕСЃС‚Рё РїР°РєРµС‚РѕРІ
static int      rx_first_pkt_seen = 0;
static uint16_t rx_curr_pkt_id = 0;



// --- CRC32 (табличный, ~4x быстрее побайтового) ---
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};


// Побайтовый CRC (для RX парсера — обрабатывает по одному байту)
static inline uint32_t calc_crc32_byte(uint32_t crc, uint8_t data) {
    return (crc >> 8) ^ crc32_table[(crc ^ data) & 0xFF];
}

// Блочный CRC (для TX — считает весь буфер за раз)
static uint32_t calc_crc32_block(const uint8_t *data, uint32_t len, uint32_t crc) {
    for (uint32_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc;
}


// Forward declaration (РѕРїСЂРµРґРµР»РµРЅРёРµ РЅРёР¶Рµ)
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);

// =========================================================
// Р§С‚РµРЅРёРµ РІСЃРµС… РіРѕС‚РѕРІС‹С… Р±Р»РѕРєРѕРІ РёР· RX FIFO (РїРѕСЃР»Рµ СЌРЅРєРѕРґРµСЂР°)
// Р’С‹Р·С‹РІР°РµС‚СЃСЏ РёР· main loop Рё РёР· send_block_to_pl
// =========================================================
void drain_rx_fifo(void) {

	bool is_val;

	if(XLlFifo_iRxOccupancy(&fifo)) {
		u32 RxWord;
		u32 preamb0, preamb1;
		u32 ReceiveLength = (XLlFifo_iRxGetLen(&fifo))/4;


if (ReceiveLength==54)
			{

				RxWord = XLlFifo_RxGetWord(&fifo);
				preamb0 = RxWord;
				RxWord = XLlFifo_RxGetWord(&fifo);
				preamb1 = RxWord;

				uint8_t fifo_buf[PL_RX_BYTES+28];

				memcpy((uint8_t*) &fifo_buf[0],  &preamb0,4);
				memcpy((uint8_t*) &fifo_buf[0]+4,&preamb1,4);

				is_val = (preamb0 == 0xb6ce261d);
				is_val = is_val && ((preamb1&0xffff0000) == 0x6d9c0000);



				uint16_t cnt_seg_in = (preamb1&0x0000ffff);
				uint16_t check = cnt_seg_in;
				uint16_t check_rx;

				for (int i=2; i < ReceiveLength; i++) {
								RxWord = XLlFifo_RxGetWord(&fifo);

								memcpy((u32*) &fifo_buf[0] + i, &RxWord, 4);


								if ((i>=3)&&(i<3+PL_RX_WORDS)) {
					            check+=  (uint16_t)fifo_buf[4*i] | ((uint16_t)fifo_buf[4*i+1] << 8);
					            check+=  (uint16_t)fifo_buf[4*i+2] | ((uint16_t)fifo_buf[4*i+3] << 8);
								}

								if (i == 3+PL_RX_WORDS) check_rx = RxWord;

							}


						if (!is_val)
				{
							//xil_printf("!p54 is_val %08x %08x \n\r",preamb0,preamb1);
				            return;
				}

	   //static u32     drain_rx_words[PL_RX_WORDS];
	   //static uint8_t drain_rx_block[PL_RX_BYTES];


		if (check_rx!=check)
			{
				//printf("cnt_seg_in=%04X check=%04X check_rx=%08X \n\r",cnt_seg_in,  check, check_rx);
                return;
			} else if (check_rx==0) return;


		//printf("cnt_seg_in=%04X check=%04X\n\r", cnt_seg_in, check);

        for (int i = 0; i < PL_RX_WORDS*4; i++) {
        	drain_rx_block[i] = fifo_buf[12+i];
        }
        radio_input_block(drain_rx_block, &port3_bridge);



	return;

			}


	else
		{
			LOG_HOT("fifo_rx_len=%d\n\r", ReceiveLength);
			for (int i=0; i < ReceiveLength; i++)  RxWord = XLlFifo_RxGetWord(&fifo);
		}

	}

}


// РҐРµР»РїРµСЂ РѕС‚РїСЂР°РІРєРё РІ FPGA (vacancy СѓР¶Рµ РїСЂРѕРІРµСЂРµРЅР° РІ tx_process)
static void send_block_to_pl(uint8_t *block) {

   struct ppos_tx ppos_tx_par;
   ppos_tx_par.creg0.word = AXI_CNTRL_REG_READ(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0);


   if (ppos_tx_par.creg0.inside.run == 0) //write pars
   {

       XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
       XLlFifo_Reset(&fifo);
       ppos_tx_par.creg0.inside.run = 1;
       AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, ppos_tx_par.creg0.word);
       return;
   }



   if (ppos_tx_par.creg0.inside.run == 1) {


    if (XLlFifo_iTxVacancy(&fifo) >= ((PL_BLOCK_SIZE+28) / 4) ) {
        u32 word;
        word = 0xb6ce261d;
        //word = swap32(word);
        XLlFifo_TxPutWord(&fifo, word);
        word = 0x6d9c0000;
        word+=cnt_tx_seg;
        uint16_t check=cnt_tx_seg;
        cnt_tx_seg++;
        XLlFifo_TxPutWord(&fifo, word);
        word = 0x00000000;
        XLlFifo_TxPutWord(&fifo, word);

        for (int i = 0; i < PL_BLOCK_SIZE; i += 4) {
            word =  (u32)block[i]        | ((u32)block[i+1] << 8) |
                   ((u32)block[i+2] << 16)| ((u32)block[i+3] << 24);


            check+=  (uint16_t)block[i] | ((uint16_t)block[i+1] << 8);
            check+=  (uint16_t)block[i+2] | ((uint16_t)block[i+3] << 8);

            XLlFifo_TxPutWord(&fifo, word);
        }



        word = 0x00000000;
        word+=check;
        XLlFifo_TxPutWord(&fifo, word);

        word = 0x00000000;
        XLlFifo_TxPutWord(&fifo, word);
        XLlFifo_TxPutWord(&fifo, word);
        XLlFifo_TxPutWord(&fifo, word);

        u32 byt = PL_BLOCK_SIZE+28;

        XLlFifo_iTxSetLen(&fifo, byt);

        dbg_tx_blocks++;
        wdt_hb_tx();
    } else {
        dbg_err_fifo_full++;
    }
}


}


// =========================================================
// Р§РђРЎРўР¬ TX (РћРўРџР РђР’РљРђ)
// =========================================================

//static void tx_push(uint8_t b) {
//    tx_ring[tx_head] = b;
//    tx_head = (tx_head + 1) % RING_SIZE;
//    tx_count++;
//}


// Блочная запись в tx_ring (вместо побайтовой tx_push)
static void tx_push_block(const uint8_t *src, uint32_t len) {

	uint32_t space_to_end = RING_SIZE - tx_head;
    uint32_t chunk = (space_to_end < len) ? space_to_end : len;

    while (len > 0) {

        memcpy(&tx_ring[tx_head], src, chunk);
        tx_head = (tx_head + chunk) % RING_SIZE;
        tx_count += chunk;
        src += chunk;
        len -= chunk;

        chunk = len;
    }
}

void tx_process() {
    while (tx_count >= BLOCK_PAYLOAD_SIZE) {
        // Если TX FIFO полон — выходим, radio_poll дошлёт
        //if (XLlFifo_iTxVacancy(&fifo) < (PL_BLOCK_SIZE / 4)) {
        if (XLlFifo_iTxVacancy(&fifo) < ((PL_BLOCK_SIZE+28) / 4) ) {


            dbg_err_tx_stall++;
            break;
        }

        uint8_t block[PL_BLOCK_SIZE];
        block[0] = global_tx_seq_counter++;

        // Блочное копирование из ring (с учётом оборачивания)
        uint32_t space_to_end = RING_SIZE - tx_tail;
        if (space_to_end >= BLOCK_PAYLOAD_SIZE) {
            memcpy(&block[1], &tx_ring[tx_tail], BLOCK_PAYLOAD_SIZE);
            tx_tail = (tx_tail + BLOCK_PAYLOAD_SIZE) % RING_SIZE;
        } else {
            memcpy(&block[1], &tx_ring[tx_tail], space_to_end);
            memcpy(&block[1 + space_to_end], &tx_ring[0], BLOCK_PAYLOAD_SIZE - space_to_end);
            tx_tail = BLOCK_PAYLOAD_SIZE - space_to_end;
        }
        tx_count -= BLOCK_PAYLOAD_SIZE;

        send_block_to_pl(block);
    }
}


err_t radio_linkoutput(struct netif *netif, struct pbuf *p) {

    /* Сюда попадаем по цепочке xemacif_input → bridgeif → vport3_linkoutput.
     * Если main loop здесь подвиснет — crash_section покажет RADIO_TX вместо
     * расплывчатого LWIP_INPUT. */
    wdt_section_mark(WDT_SEC_RADIO_TX);

    /* Робастность под перегрузкой: xemacif_input может за один вызов разгребать
     * большой GEM-RX backlog и не возвращаться в main loop секундами, где живёт
     * единственный wdt_kick(). Кикаем WDT прямо отсюда (троттлингом) — пока
     * пакеты реально текут через radio path, это прогресс, а не hang. Ставим
     * ДО drop-проверки, чтобы поток дропаемых пакетов тоже держал таймер. */
    wdt_progress_kick();

    if ((RING_SIZE - tx_count) < (p->tot_len + 10)) {
        dbg_err_tx_reject++;
        return ERR_MEM;
    }

    dbg_tx_pkts++;

    // CRC32 по всему пакету (блочный — один table lookup на байт)
    uint32_t crc = 0xFFFFFFFF;
    struct pbuf *q;
    for(q = p; q != NULL; q = q->next) {
        crc = calc_crc32_block((uint8_t*)q->payload, q->len, crc);
    }
    crc = ~crc;

    // Заголовок (10 байт) — побайтово, мелкий overhead
    uint8_t hdr[6];
    hdr[0] = MAGIC_H;
    hdr[1] = MAGIC_L;
    hdr[2] = (global_tx_pkt_counter >> 8) & 0xFF;
    hdr[3] = global_tx_pkt_counter & 0xFF;
    hdr[4] = (p->tot_len >> 8) & 0xFF;
    hdr[5] = (p->tot_len & 0xFF);
    global_tx_pkt_counter++;
    tx_push_block(hdr, 6);

    // Данные пакета — блочная запись (memcpy вместо побайтовой)
    for(q = p; q != NULL; q = q->next) {
        tx_push_block((uint8_t*)q->payload, q->len);
    }

    // CRC (4 байта)
    uint8_t crc_bytes[4];
    crc_bytes[0] = (crc >> 24) & 0xFF;
    crc_bytes[1] = (crc >> 16) & 0xFF;
    crc_bytes[2] = (crc >> 8) & 0xFF;
    crc_bytes[3] = crc & 0xFF;
    tx_push_block(crc_bytes, 4);

    tx_process();
    return ERR_OK;
}


void radio_tx_periodic_flush(void) {
    if (tx_count > 0 && tx_count < BLOCK_PAYLOAD_SIZE) {
        uint32_t pad_needed = BLOCK_PAYLOAD_SIZE - tx_count;
        if ((RING_SIZE - tx_count) >= pad_needed) {
            // Заполняем нулями блочно
            uint8_t zeros[BLOCK_PAYLOAD_SIZE];
            memset(zeros, 0x00, pad_needed);
            tx_push_block(zeros, pad_needed);
            tx_process();
        }
    }
}


void radio_poll(void) {
    drain_rx_fifo();
    tx_process();  // РґРѕСЃС‹Р»Р°РµС‚ Р±Р»РѕРєРё РёР· tx_ring РµСЃР»Рё РµСЃС‚СЊ РјРµСЃС‚Рѕ РІ FIFO
}

// =========================================================
// Р§РђРЎРўР¬ RX (РџР Р�Р•Рњ)
// =========================================================

void radio_input_block(uint8_t *block_188, struct netif *inp_netif) {
    dbg_rx_blocks++;
    wdt_hb_rx();

    uint8_t seq = block_188[0];
    if (!rx_first_block_seen) {
        rx_first_block_seen = 1;
    } else if ((uint8_t)(rx_last_seq + 1) != seq) {
        dbg_err_seq_gap++;
    }
    rx_last_seq = seq;

    for (int i = 0; i < BLOCK_PAYLOAD_SIZE; i++) {
        if (rx_count < RING_SIZE) {
            rx_ring[rx_head] = block_188[i + 1];
            rx_head = (rx_head + 1) % RING_SIZE;
            rx_count++;
        } else {
            dbg_err_rx_overflow++;
        }
    }

    //printf("rx_count=%d\r\n", rx_count);


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

            case ST_ID_H:
                rx_curr_pkt_id = ((uint16_t)b << 8);
                rx_tail = (rx_tail + 1) % RING_SIZE; rx_count--;
                rx_state = ST_ID_L;
                break;

            case ST_ID_L:
                rx_curr_pkt_id |= b;
                if (!rx_first_pkt_seen) {
                    rx_first_pkt_seen = 1;
                } else if ((uint16_t)(rx_last_pkt_id + 1) != rx_curr_pkt_id) {
                    dbg_err_pkt_gap++;
                }
                rx_last_pkt_id = rx_curr_pkt_id;
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


              // printf("rx_recv_crc = %08X rx_calc_crc= %08X\r\n",rx_recv_crc, rx_calc_crc);


                // РџР РћР’Р•Р РљРђ CRC
                if (rx_recv_crc == rx_calc_crc) {
                    dbg_rx_pkts++;

                    //struct pbuf *p = pbuf_alloc(PBUF_RAW, rx_pkt_len, PBUF_RAM);
                    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, rx_pkt_len, PBUF_RAM);

                    if (p != NULL) {

                        memcpy(p->payload, rx_pkt_buffer, rx_pkt_len);

                       // printf("SP l=%d\r\n",rx_pkt_len);


                    	if (0)
                    	{
                           struct eth_hdr *ethhdr = (struct eth_hdr*)p->payload;
                            xil_printf("[seg2pbuf]   ethernet_input: dest:%02X:%02X:%02X:%02X:%02X:%02X, src:%02X:%02X:%02X:%02X:%02X:%02X, type:%04X  len: %4d  \n\r",
                           (unsigned char)ethhdr->dest.addr[0], (unsigned char)ethhdr->dest.addr[1], (unsigned char)ethhdr->dest.addr[2],
                           (unsigned char)ethhdr->dest.addr[3], (unsigned char)ethhdr->dest.addr[4], (unsigned char)ethhdr->dest.addr[5],
                           (unsigned char)ethhdr->src.addr[0],  (unsigned char)ethhdr->src.addr[1],  (unsigned char)ethhdr->src.addr[2],
                           (unsigned char)ethhdr->src.addr[3],  (unsigned char)ethhdr->src.addr[4],  (unsigned char)ethhdr->src.addr[5],
                    	   lwip_htons(ethhdr->type) , p->tot_len  );
                    	}



                        if (inp_netif->input != NULL) {
                            if (inp_netif->input(p, inp_netif) != ERR_OK) {

                            	printf("NOT input\r\n");
                            	pbuf_free(p);
                            }
                        } else {
                            pbuf_free(p);
                            printf("NOT alloc\r\n");
                        }
                    }


                } else {
                    dbg_err_crc++;
                    if (!crc_dump_done) {
                        crc_dump_done = 1;
                        xil_printf("[CRC FAIL] pkt_id=%u len=%u calc=0x%08X recv=0x%08X\r\n",
                                   rx_curr_pkt_id, rx_pkt_len, rx_calc_crc, rx_recv_crc);
                        xil_printf("First 48 bytes: ");
                        for (int d = 0; d < 48 && d < rx_pkt_len; d++)
                            xil_printf("%02X ", rx_pkt_buffer[d]);
                        xil_printf("\r\n");
                        xil_printf("Last 16 bytes: ");
                        for (int d = (rx_pkt_len > 16 ? rx_pkt_len - 16 : 0); d < rx_pkt_len; d++)
                            xil_printf("%02X ", rx_pkt_buffer[d]);
                        xil_printf("\r\n");
                    }
                }
                rx_state = ST_MAGIC_H;
                break;
        }
    }
}

// Р’С‹РІРѕРґ СЃС‚Р°С‚РёСЃС‚РёРєРё
void radio_stream_print_stats(void) {
    xil_printf("TX: %u Pkt -> %u Blk (FIFO full: %u, stall: %u, reject: %u)\r\n",
        dbg_tx_pkts, dbg_tx_blocks, dbg_err_fifo_full, dbg_err_tx_stall, dbg_err_tx_reject);
    xil_printf("RX: %u Blk (seg gap: %u, overflow: %u) -> %u Pkt (pkt gap: %u, CRC err: %u)\r\n",
        dbg_rx_blocks, dbg_err_seq_gap, dbg_err_rx_overflow,
        dbg_rx_pkts, dbg_err_pkt_gap, dbg_err_crc);
    xil_printf("RING: tx_count=%u/%u\r\n", tx_count, RING_SIZE);
}

void radio_stream_init(void) {
    tx_head = tx_tail = tx_count = 0;
    rx_head = rx_tail = rx_count = 0;
}

void radio_stream_reset_stats(void) {
    dbg_tx_pkts = 0;
    dbg_tx_blocks = 0;
    dbg_rx_blocks = 0;
    dbg_rx_pkts = 0;
    dbg_err_seq_gap = 0;
    dbg_err_crc = 0;
    dbg_err_fifo_full = 0;
    dbg_err_rx_overflow = 0;
    dbg_err_pkt_gap = 0;
    dbg_err_tx_stall = 0;
    dbg_err_tx_reject = 0;
    global_tx_pkt_counter = 0;
    global_tx_seq_counter = 0;
    rx_first_block_seen = 0;
    rx_first_pkt_seen = 0;
    rx_state = ST_MAGIC_H;
    tx_head = tx_tail = tx_count = 0;
    rx_head = rx_tail = rx_count = 0;
    crc_dump_done = 0;
    xil_printf("[RADIO] Stats + state RESET\r\n");
}

void radio_stream_get_stats(radio_stats_t *out) {
    if (!out) return;
    out->tx_pkts        = dbg_tx_pkts;
    out->tx_blocks      = dbg_tx_blocks;
    out->err_fifo_full  = dbg_err_fifo_full;
    out->rx_blocks      = dbg_rx_blocks;
    out->err_seq_gap    = dbg_err_seq_gap;
    out->err_rx_overflow = dbg_err_rx_overflow;
    out->rx_pkts        = dbg_rx_pkts;
    out->err_pkt_gap    = dbg_err_pkt_gap;
    out->err_crc        = dbg_err_crc;
}


void radio_stream_reset_rx_ids(void) {
    rx_first_block_seen = 0;
    rx_first_pkt_seen   = 0;
}

/* ===================================================================
 *  Recovery handlers
 *  ------------------------------------------------------------------
 *  Локальный "soft restart" одной из сторон radio path без PS reset.
 *  Чистит:
 *    - соответствующую сторону AXI FIFO (TX или RX)
 *    - software state (ring, parser state, counters)
 *  Stats-счётчики ошибок НЕ обнуляются — они нужны для диагностики
 *  частоты срабатываний recovery.
 * =================================================================== */
void radio_recover_tx(void) {
    XLlFifo_TxReset(&fifo);
    tx_head = tx_tail = tx_count = 0;
    global_tx_pkt_counter = 0;
    global_tx_seq_counter = 0;
    xil_printf("[RECOV] radio_tx: FIFO TX reset + ring/counters cleared\r\n");
}

void radio_recover_rx(void) {
    XLlFifo_RxReset(&fifo);
    rx_head = rx_tail = rx_count = 0;
    rx_state = ST_MAGIC_H;
    rx_pkt_len = 0;
    rx_pkt_received = 0;
    rx_calc_crc = 0;
    rx_recv_crc = 0;
    rx_first_block_seen = 0;
    rx_first_pkt_seen = 0;
    xil_printf("[RECOV] radio_rx: FIFO RX reset + parser state cleared\r\n");
}

void radio_stream_set_loopback(int enable) {    loopback_enabled = enable;
    xil_printf("[RADIO] Loopback %s\r\n", enable ? "ENABLED" : "DISABLED");
}

int radio_stream_get_loopback(void) {
    return loopback_enabled;
}
