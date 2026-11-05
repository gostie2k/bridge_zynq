#include "bridge_func.h"
#include "radio_stream.h"
#include "xil_printf.h"
#include "xllfifo.h"
#include <string.h>
#include <stdbool.h>
#include "modem.h"
#include "watchdog.h"   /* wdt_progress_kick */
#include "xtime_l.h"

#include "lwip/prot/ethernet.h"
#include "lwip/prot/ip4.h"

/* RX debug channel */
struct deb_chan_rx buf_deb_chan_rx[BUF_DEB_CHAN_RX_DEPTH];
uint32_t buf_deb_chan_rx_wad = 0;
uint32_t buf_deb_chan_rx_rad = 0;

extern struct netif port3_bridge;

#define PL_BLOCK_SIZE 188
#define BLOCK_HEADER_SIZE 3   /* [0]=stream bit + seq, [1..2]=src device_id */
#define BLOCK_PAYLOAD_SIZE (PL_BLOCK_SIZE - BLOCK_HEADER_SIZE)   /* 185 */

#define MAGIC_H 0xFA
#define MAGIC_L 0xCE

/* Ring depth is latency: depth / drain rate. A big ring buys burst tolerance
 * only; a frame that late gets dropped by the decoder anyway. */
#define RING_SIZE (188 * 1024)

/* Two streams share the radio: bulk DATA and latency-critical TELEMETRY.
 * Separate rings and FIFOs. Class by dst UDP port, plus ARP and ICMP. */
#define TLM_UDP_PORTS  { 5005, 5006 }   /* TODO: real ports */

#define STREAM_DATA 0
#define STREAM_TLM  1
#define STREAM_CNT  2

/* Small on purpose: its depth is the telemetry latency */
#define TLM_RING_SIZE (8 * 1024)

extern XLlFifo fifo;          /* MM_S_0: all RX + telemetry TX */
extern XLlFifo fifo_npr_tx;   /* MM_S_2: data TX */

typedef struct {
    uint8_t  *ring;
    uint32_t  size;
    uint32_t  head, tail, count;
    uint16_t  pkt_id;
    uint8_t   seq;           /* 7 bits on air */
    uint32_t  dbg_pkts;
    uint32_t  dbg_reject;    /* ring was full */
    uint32_t  dbg_count_max;
} tx_stream_t;

static uint8_t tx_ring_data[RING_SIZE];
static uint8_t tx_ring_tlm[TLM_RING_SIZE];

static tx_stream_t tx_str[STREAM_CNT] = {
    { .ring = tx_ring_data, .size = RING_SIZE },
    { .ring = tx_ring_tlm,  .size = TLM_RING_SIZE },
};

static const uint16_t tlm_ports[] = TLM_UDP_PORTS;
#define TLM_PORTS_N (sizeof(tlm_ports) / sizeof(tlm_ports[0]))

/* Which stream does this frame belong to */
static int classify(struct pbuf *p)
{
    if (p == NULL || p->len < sizeof(struct eth_hdr)) return STREAM_DATA;

    struct eth_hdr *eth = (struct eth_hdr *)p->payload;
    uint16_t type = lwip_ntohs(eth->type);

    if (type == ETHTYPE_ARP) return STREAM_TLM;
    if (type != ETHTYPE_IP)  return STREAM_DATA;
    if (p->len < sizeof(struct eth_hdr) + 20) return STREAM_DATA;

    uint8_t *ip = (uint8_t *)p->payload + sizeof(struct eth_hdr);
    uint8_t proto = ip[9];
    if (proto == 1)  return STREAM_TLM;    /* ICMP */
    if (proto != 17) return STREAM_DATA;   /* only UDP is classified by port */

    uint32_t ihl = (uint32_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || p->len < sizeof(struct eth_hdr) + ihl + 8) return STREAM_DATA;

    uint8_t *udp = ip + ihl;
    uint16_t dport = ((uint16_t)udp[2] << 8) | udp[3];
    for (unsigned i = 0; i < TLM_PORTS_N; i++)
        if (dport == tlm_ports[i]) return STREAM_TLM;

    return STREAM_DATA;
}

static int  loopback_enabled = 0;  /* 1 = software loopback */


#define PL_RX_WORDS  47
#define PL_RX_BYTES  (PL_RX_WORDS * 4)

static u32     drain_rx_words[PL_RX_WORDS];
static uint8_t drain_rx_block[PL_RX_BYTES];

uint16_t cnt_tx_seg=0;




static u32 dbg_tx_blocks = 0;
static u32 dbg_rx_blocks = 0;
static u32 dbg_rx_pkts = 0;
static u32 dbg_err_seq_gap = 0;   /* lost segments */
static u32 dbg_err_crc = 0;
static u32 dbg_err_fifo_full = 0; /* FIFO full, block dropped */
static u32 dbg_err_rx_overflow = 0;
static u32 dbg_err_pkt_gap = 0;   /* packet id gaps on RX */

static int crc_dump_done = 0;     /* one-shot CRC dump */
static u32 dbg_err_tx_stall = 0;  /* tx_process aborted, TX FIFO full */

/* RX path counters, reported by 0x25 */
static u32 dbg_rx_len_new    = 0;  /* 47+DEB words, current format */
static u32 dbg_rx_len_legacy = 0;  /* always 0, kept so 0x25 keeps its shape */
static u32 dbg_rx_len_other  = 0;
static u32 dbg_rx_len_last   = 0;  /* length of the last odd frame */
static u32 dbg_rx_magic      = 0;  /* FA CE marks found */
static u8  dbg_rx_first16[16];

/* Learned once at init, it is a build-time IP parameter */
static u32 g_fifo_tx_depth  = 0;   /* telemetry, MM_S_0 */
static u32 g_fifo_data_depth = 0;  /* data, MM_S_2 */

/* ---- RX parser ---- */
typedef enum {
    ST_MAGIC_H, ST_MAGIC_L,
    ST_ID_H,    ST_ID_L,
    ST_LEN_H,   ST_LEN_L,
    ST_DATA,
    ST_CRC0, ST_CRC1, ST_CRC2, ST_CRC3
} rx_state_t;

/* One reassembly context per stream: on air they interleave block by block */
typedef struct {
    uint8_t   *ring;
    uint32_t   size, head, tail, count;
    rx_state_t state;
    uint16_t   pkt_len, pkt_received;
    uint8_t    pkt_buffer[1600];
    uint32_t   calc_crc, recv_crc;
    uint8_t    last_seq;         /* 7-bit block counter of this stream */
    int        first_block_seen;
    uint16_t   last_pkt_id, curr_pkt_id;
    int        first_pkt_seen;
} rx_stream_t;

/* Never hold more than one block: radio_input_block drains it in the same call */
static uint8_t rx_ring_data[RING_SIZE];
static uint8_t rx_ring_tlm[TLM_RING_SIZE];

static rx_stream_t rx_str[STREAM_CNT] = {
    { .ring = rx_ring_data, .size = RING_SIZE,     .state = ST_MAGIC_H },
    { .ring = rx_ring_tlm,  .size = TLM_RING_SIZE, .state = ST_MAGIC_H },
};

static uint16_t rx_last_src_id = 0;   /* source device_id from block header */



/* CRC32 table */
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


/* per-byte, for the RX parser */
static inline uint32_t calc_crc32_byte(uint32_t crc, uint8_t data) {
    return (crc >> 8) ^ crc32_table[(crc ^ data) & 0xFF];
}

/* whole buffer, for TX */
static uint32_t calc_crc32_block(const uint8_t *data, uint32_t len, uint32_t crc) {
    for (uint32_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc;
}


/* defined below */
void radio_input_block(uint8_t *block_188, struct netif *inp_netif);

/* Frame from the PL FIFO: 47 segment words + RX_CHAN0_DEB_W diag words.
 * Diag goes to buf_deb_chan_rx, send_deb_rx_append() picks it up.
 * Two RX channels differ only in chan_id and whether the segment
 * goes to the bridge, so one body with two thin wrappers. */
static void drain_chan_fifo(XLlFifo *fifo_data, uint8_t chan_id, bool to_bridge)
{
	if (!XLlFifo_iRxOccupancy(fifo_data)) return;

	u32 RxWord;
	u32 ReceiveLength = (XLlFifo_iRxGetLen(fifo_data)) / 4;

	if (ReceiveLength != 47 + RX_CHAN0_DEB_W) {
		/* data channel only: mixing the control channel makes 0x25 unreadable */
		if (to_bridge) { dbg_rx_len_other++; dbg_rx_len_last = ReceiveLength; }
		xil_printf("fifo_rx_len=%d\n\r", ReceiveLength);
		for (u32 i = 0; i < ReceiveLength; i++) RxWord = XLlFifo_RxGetWord(fifo_data);
		return;
	}

	if (to_bridge) dbg_rx_len_new++;

	uint8_t fifo_buf[PL_RX_BYTES + RX_CHAN0_DEB_W * 4];

	for (int i = 0; i < 47; i++) {
		RxWord = XLlFifo_RxGetWord(fifo_data);
		memcpy((u32*) &fifo_buf[0] + i, &RxWord, 4);
	}

	struct deb_chan_rx *d = &buf_deb_chan_rx[buf_deb_chan_rx_wad];

	d->cnan_id = chan_id;
	d->cnt_seg = fifo_buf[0];

	u32 ap_dat0 = XLlFifo_RxGetWord(fifo_data);
	d->mer = ap_dat0;

	u32 ap_dat1 = XLlFifo_RxGetWord(fifo_data);
	d->ad9361_gain = (ap_dat1 >> 8) & 0x000000ff;
	d->ad9361_rssi = (ap_dat1 >> 0) & 0x000000ff;

	u32 ap_dat2 = XLlFifo_RxGetWord(fifo_data);
	d->rs_dec_stat.inside.rs_dec_bit_err_10 =  ap_dat2        & 0x000000ff;
	d->rs_dec_stat.inside.rs_dec_bit_err_01 = (ap_dat2 >>  8) & 0x000000ff;
	d->rs_dec_stat.inside.rs_dec_err_cnt    = (ap_dat2 >> 16) & 0x0000001f;
	d->rs_dec_stat.inside.rs_dec_err_found  = (ap_dat2 >> 21) & 0x00000001;
	d->rs_dec_stat.inside.rs_dec_fail       = (ap_dat2 >> 22) & 0x00000001;

	u32 ap_dat3 = XLlFifo_RxGetWord(fifo_data);
	d->vit_err_cnt = ap_dat3 & 0x000007ff;

	u32 ap_dat4 = XLlFifo_RxGetWord(fifo_data);
	d->del_ack = ap_dat4;

	/* read the flag BEFORE advancing the ring, else it reads the next slot */
	const bool rs_ok = (d->rs_dec_stat.inside.rs_dec_fail == 0);

	buf_deb_chan_rx_wad++;
	if (buf_deb_chan_rx_wad == BUF_DEB_CHAN_RX_DEPTH) buf_deb_chan_rx_wad = 0;

	/* to the bridge: data channel only, and only what RS recovered */
	if (to_bridge && rs_ok) {
		for (int i = 0; i < PL_RX_WORDS * 4; i++) drain_rx_block[i] = fifo_buf[0 + i];
		radio_input_block(drain_rx_block, &port3_bridge);
	}
}

void drain_rx_fifo(XLlFifo* fifo_data)
{
	drain_chan_fifo(fifo_data, 0, true);
}

void drain_cntrl_tx_fifo(XLlFifo* fifo_data)
{
	drain_chan_fifo(fifo_data, 1, false);
}





/* send to the PL, vacancy already checked */
static void send_block_to_pl(uint8_t *block, XLlFifo *f) {

   struct ppos_tx ppos_tx_par;
   ppos_tx_par.creg0.word = AXI_CNTRL_REG_READ(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0);


   if (ppos_tx_par.creg0.inside.run == 0) //write pars
   {

       XLlFifo_IntClear(f, 0xFFFFFFFF);
       XLlFifo_Reset(f);
       ppos_tx_par.creg0.inside.run = 1;
       AXI_CNTRL_REG_WRITE(AXI_CNTRL_REG_BASEADDR + TX_CHAN0_PAR_OFFSET, 0, ppos_tx_par.creg0.word);
       return;
   }



   if (ppos_tx_par.creg0.inside.run == 1) {


    if (XLlFifo_iTxVacancy(f) >= ((PL_BLOCK_SIZE+0) / 4) ) {

        u32 word;


        for (u32 i = 0; i < PL_BLOCK_SIZE + 0; i += 4) {

        	if (i < PL_BLOCK_SIZE)
        	{

            word =  (u32)block[i]        | ((u32)block[i+1] << 8) |
                   ((u32)block[i+2] << 16)| ((u32)block[i+3] << 24);




        	} else word =0;


            XLlFifo_TxPutWord(f, word);
        }




        u32 byt = PL_BLOCK_SIZE+0;

        XLlFifo_iTxSetLen(f, byt);

        dbg_tx_blocks++;
    } else {
        dbg_err_fifo_full++;
    }
}


}


/* ---- TX ---- */

/* Block write into a stream ring, handles wrap-around */
static void tx_push_block(tx_stream_t *s, const uint8_t *src, uint32_t len) {

    uint32_t space_to_end = s->size - s->head;
    uint32_t chunk = (space_to_end < len) ? space_to_end : len;

    while (len > 0) {

        memcpy(&s->ring[s->head], src, chunk);
        s->head = (s->head + chunk) % s->size;
        s->count += chunk;
        src += chunk;
        len -= chunk;

        chunk = len;
    }

    /* high-water mark: a 10 Hz poll misses the peaks */
    if (s->count > s->dbg_count_max) s->dbg_count_max = s->count;
}

/* Cut one block off a stream and hand it to the PL */
static void tx_emit_block(tx_stream_t *s, int stream_id, XLlFifo *f) {
    uint8_t block[PL_BLOCK_SIZE];

    /* [0] = stream bit (7) + 7-bit per-stream block counter */
    block[0] = (uint8_t)((stream_id << 7) | (s->seq & 0x7F));
    s->seq = (uint8_t)((s->seq + 1) & 0x7F);

    /* src device_id in every block, for point-to-multipoint */
    block[1] = (current_config.device_id >> 8) & 0xFF;
    block[2] =  current_config.device_id       & 0xFF;

    uint32_t space_to_end = s->size - s->tail;
    if (space_to_end >= BLOCK_PAYLOAD_SIZE) {
        memcpy(&block[BLOCK_HEADER_SIZE], &s->ring[s->tail], BLOCK_PAYLOAD_SIZE);
        s->tail = (s->tail + BLOCK_PAYLOAD_SIZE) % s->size;
    } else {
        memcpy(&block[BLOCK_HEADER_SIZE], &s->ring[s->tail], space_to_end);
        memcpy(&block[BLOCK_HEADER_SIZE + space_to_end], &s->ring[0],
               BLOCK_PAYLOAD_SIZE - space_to_end);
        s->tail = BLOCK_PAYLOAD_SIZE - space_to_end;
    }
    s->count -= BLOCK_PAYLOAD_SIZE;

    send_block_to_pl(block, f);
}

/* No arbiter here: with two FIFOs the PL decides the order on air.
 * Two independent loops, so one full FIFO cannot stall the other. */
static void tx_drain_stream(int stream_id, XLlFifo *f)
{
    tx_stream_t *s = &tx_str[stream_id];

    while (s->count >= BLOCK_PAYLOAD_SIZE) {
        /* exactly PL_BLOCK_SIZE goes into the FIFO, no framing */
        if (XLlFifo_iTxVacancy(f) < ((PL_BLOCK_SIZE + 0) / 4)) {
            dbg_err_tx_stall++;
            break;
        }
        tx_emit_block(s, stream_id, f);
    }
}

void tx_process() {
    tx_drain_stream(STREAM_TLM,  &fifo);
    tx_drain_stream(STREAM_DATA, &fifo_npr_tx);
}



err_t radio_linkoutput(struct netif *netif, struct pbuf *p) {

    /* packets flowing = progress, not a hang. Before the drop check on purpose. */
    wdt_section_mark(WDT_SEC_RADIO_TX);
    wdt_progress_kick();

    int stream_id = classify(p);
    tx_stream_t *s = &tx_str[stream_id];

    if ((s->size - s->count) < (uint32_t)(p->tot_len + 10)) {
        s->dbg_reject++;
        return ERR_MEM;
    }

    s->dbg_pkts++;

    /* CRC32 over the whole packet */
    uint32_t crc = 0xFFFFFFFF;
    struct pbuf *q;
    for(q = p; q != NULL; q = q->next) {
        crc = calc_crc32_block((uint8_t*)q->payload, q->len, crc);
    }
    crc = ~crc;

    /* header, 10 bytes total with the CRC */
    uint8_t hdr[6];
    hdr[0] = MAGIC_H;
    hdr[1] = MAGIC_L;
    hdr[2] = (s->pkt_id >> 8) & 0xFF;
    hdr[3] = s->pkt_id & 0xFF;
    hdr[4] = (p->tot_len >> 8) & 0xFF;
    hdr[5] = (p->tot_len & 0xFF);
    s->pkt_id++;
    tx_push_block(s, hdr, 6);

    /* packet data */
    for(q = p; q != NULL; q = q->next) {
        tx_push_block(s, (uint8_t*)q->payload, q->len);
    }

    /* CRC, 4 bytes */
    uint8_t crc_bytes[4];
    crc_bytes[0] = (crc >> 24) & 0xFF;
    crc_bytes[1] = (crc >> 16) & 0xFF;
    crc_bytes[2] = (crc >> 8) & 0xFF;
    crc_bytes[3] = crc & 0xFF;
    tx_push_block(s, crc_bytes, 4);

    tx_process();
    return ERR_OK;
}


void radio_tx_periodic_flush(void) {
    /* Pad a tail shorter than one block, else it waits for more traffic */
    for (int i = 0; i < STREAM_CNT; i++) {
        tx_stream_t *s = &tx_str[i];
        if (s->count > 0 && s->count < BLOCK_PAYLOAD_SIZE) {
            uint32_t pad_needed = BLOCK_PAYLOAD_SIZE - s->count;
            if ((s->size - s->count) >= pad_needed) {
                uint8_t zeros[BLOCK_PAYLOAD_SIZE];
                memset(zeros, 0x00, pad_needed);
                tx_push_block(s, zeros, pad_needed);
            }
        }
    }
    tx_process();
}
void radio_poll(void) {
    drain_rx_fifo(&fifo);
    tx_process();  /* drains the rings if the FIFO has room */
}

/* ---- RX ---- */

void radio_input_block(uint8_t *block_188, struct netif *inp_netif) {
    dbg_rx_blocks++;
    for (int i = 0; i < 16; i++) dbg_rx_first16[i] = block_188[i];

    /* demux on the stream bit first, else the parsers splice the streams */
    int stream_id = (block_188[0] >> 7) & 1;
    uint8_t seq   = block_188[0] & 0x7F;
    rx_stream_t *st = &rx_str[stream_id];

    /* src device_id: just read for now, multipoint will demux on it */
    rx_last_src_id = ((uint16_t)block_188[1] << 8) | block_188[2];

    if (!st->first_block_seen) {
        st->first_block_seen = 1;
    } else if (((st->last_seq + 1) & 0x7F) != seq) {
        dbg_err_seq_gap++;
    }
    st->last_seq = seq;

    for (int i = 0; i < BLOCK_PAYLOAD_SIZE; i++) {
        if (st->count < st->size) {
            st->ring[st->head] = block_188[i + BLOCK_HEADER_SIZE];
            st->head = (st->head + 1) % st->size;
            st->count++;
        } else {
            dbg_err_rx_overflow++;
        }
    }

    while (st->count > 0) {
        uint8_t b = st->ring[st->tail];

        if (st->state == ST_DATA) {
             st->calc_crc = calc_crc32_byte(st->calc_crc, b);
        }

        switch (st->state) {
            case ST_MAGIC_H:
                if (b == MAGIC_H) st->state = ST_MAGIC_L;
                st->tail = (st->tail + 1) % st->size; st->count--;
                break;

            case ST_MAGIC_L:
                if (b == MAGIC_L) dbg_rx_magic++;
                if (b == MAGIC_L) st->state = ST_ID_H;
                else st->state = ST_MAGIC_H;
                st->tail = (st->tail + 1) % st->size; st->count--;
                break;

            case ST_ID_H:
                st->curr_pkt_id = ((uint16_t)b << 8);
                st->tail = (st->tail + 1) % st->size; st->count--;
                st->state = ST_ID_L;
                break;

            case ST_ID_L:
                st->curr_pkt_id |= b;
                if (!st->first_pkt_seen) {
                    st->first_pkt_seen = 1;
                } else if ((uint16_t)(st->last_pkt_id + 1) != st->curr_pkt_id) {
                    dbg_err_pkt_gap++;
                }
                st->last_pkt_id = st->curr_pkt_id;
                st->tail = (st->tail + 1) % st->size; st->count--;
                st->state = ST_LEN_H;
                break;

            case ST_LEN_H:
                st->pkt_len = (b << 8);
                st->tail = (st->tail + 1) % st->size; st->count--;
                st->state = ST_LEN_L;
                break;

            case ST_LEN_L:
                st->pkt_len |= b;
                st->tail = (st->tail + 1) % st->size; st->count--;
                if (st->pkt_len > 1520 || st->pkt_len == 0) {
                    st->state = ST_MAGIC_H;
                } else {
                    st->pkt_received = 0;
                    st->calc_crc = 0xFFFFFFFF;
                    st->state = ST_DATA;
                }
                break;

            case ST_DATA:
                st->pkt_buffer[st->pkt_received++] = b;
                st->tail = (st->tail + 1) % st->size; st->count--;
                if (st->pkt_received == st->pkt_len) {
                    st->calc_crc = ~st->calc_crc;
                    st->state = ST_CRC0;
                }
                break;

            case ST_CRC0: st->recv_crc = ((uint32_t)b << 24); st->state = ST_CRC1; st->tail = (st->tail + 1) % st->size; st->count--; break;
            case ST_CRC1: st->recv_crc |= ((uint32_t)b << 16); st->state = ST_CRC2; st->tail = (st->tail + 1) % st->size; st->count--; break;
            case ST_CRC2: st->recv_crc |= ((uint32_t)b << 8);  st->state = ST_CRC3; st->tail = (st->tail + 1) % st->size; st->count--; break;
            case ST_CRC3: st->recv_crc |= b;
                st->tail = (st->tail + 1) % st->size; st->count--;

                if (st->recv_crc == st->calc_crc) {
                    dbg_rx_pkts++;

                    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, st->pkt_len, PBUF_RAM);

                    if (p != NULL) {
                        memcpy(p->payload, st->pkt_buffer, st->pkt_len);

                        /* both streams are plain Ethernet frames, the bridge
                         * delivers them by MAC */
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
                        xil_printf("[CRC FAIL] stream=%d pkt_id=%u len=%u calc=0x%08X recv=0x%08X\r\n",
                                   stream_id, st->curr_pkt_id, st->pkt_len,
                                   st->calc_crc, st->recv_crc);
                    }
                }
                st->state = ST_MAGIC_H;
                break;
        }
    }
}

/* ---- stats ---- */
void radio_stream_print_stats(void) {
    xil_printf("TX data: %u Pkt (reject %u)   TX tlm: %u Pkt (reject %u)\r\n",
        tx_str[STREAM_DATA].dbg_pkts, tx_str[STREAM_DATA].dbg_reject,
        tx_str[STREAM_TLM].dbg_pkts,  tx_str[STREAM_TLM].dbg_reject);
    xil_printf("TX: %u Blk (FIFO full: %u, stall: %u)\r\n",
        dbg_tx_blocks, dbg_err_fifo_full, dbg_err_tx_stall);
    xil_printf("RX: %u Blk (seg gap: %u, overflow: %u) -> %u Pkt (pkt gap: %u, CRC err: %u)  src_id=%u\r\n",
        dbg_rx_blocks, dbg_err_seq_gap, dbg_err_rx_overflow,
        dbg_rx_pkts, dbg_err_pkt_gap, dbg_err_crc, rx_last_src_id);
    xil_printf("RING: data=%u/%u  tlm=%u/%u\r\n",
        tx_str[STREAM_DATA].count, tx_str[STREAM_DATA].size,
        tx_str[STREAM_TLM].count,  tx_str[STREAM_TLM].size);
}

void radio_stream_init(void) {
    for (int i = 0; i < STREAM_CNT; i++) {
        tx_str[i].head = tx_str[i].tail = tx_str[i].count = 0;
        rx_str[i].head = rx_str[i].tail = rx_str[i].count = 0;
    }
}

void radio_stream_reset_stats(void) {
    dbg_tx_blocks = 0;
    dbg_rx_blocks = 0;
    dbg_rx_pkts = 0;
    dbg_err_seq_gap = 0;
    dbg_err_crc = 0;
    dbg_err_fifo_full = 0;
    dbg_err_rx_overflow = 0;
    dbg_err_pkt_gap = 0;
    dbg_err_tx_stall = 0;
    for (int i = 0; i < STREAM_CNT; i++) {
        tx_str[i].head = tx_str[i].tail = tx_str[i].count = 0;
        tx_str[i].pkt_id = 0; tx_str[i].seq = 0;
        tx_str[i].dbg_pkts = tx_str[i].dbg_reject = tx_str[i].dbg_count_max = 0;
        rx_str[i].head = rx_str[i].tail = rx_str[i].count = 0;
        rx_str[i].state = ST_MAGIC_H;
        rx_str[i].first_block_seen = rx_str[i].first_pkt_seen = 0;
    }
    crc_dump_done = 0;
    xil_printf("[RADIO] Stats + state RESET\r\n");
}

void radio_stream_get_stats(radio_stats_t *out) {
    if (!out) return;
    out->tx_pkts        = tx_str[STREAM_DATA].dbg_pkts + tx_str[STREAM_TLM].dbg_pkts;
    out->tx_blocks      = dbg_tx_blocks;
    out->err_fifo_full  = dbg_err_fifo_full;
    out->rx_blocks      = dbg_rx_blocks;
    out->err_seq_gap    = dbg_err_seq_gap;
    out->err_rx_overflow = dbg_err_rx_overflow;
    out->rx_pkts        = dbg_rx_pkts;
    out->err_pkt_gap    = dbg_err_pkt_gap;
    out->err_crc        = dbg_err_crc;
}


/* ---- Queue / FIFO telemetry ---- */

static uint32_t now_ms(void) {
    XTime t;
    XTime_GetTime(&t);
    return (uint32_t)((t * 1000ULL) / COUNTS_PER_SECOND);
}

void radio_stream_set_fifo_depth_data(uint32_t words) {
    g_fifo_data_depth = words;
}

void radio_stream_set_fifo_depth(uint32_t words) {
    g_fifo_tx_depth = words;
}

void radio_stream_get_queue_stats(radio_queue_stats_t *out) {
    if (!out) return;
    out->t_ms            = now_ms();
    /* Fields 2..12 are the DATA stream, telemetry is appended after them */
    out->ring_size       = tx_str[STREAM_DATA].size;
    out->tx_count        = tx_str[STREAM_DATA].count;
    out->tx_count_max    = tx_str[STREAM_DATA].dbg_count_max;
    out->tx_blocks       = dbg_tx_blocks;
    out->tx_pkts         = tx_str[STREAM_DATA].dbg_pkts;
    out->err_tx_reject   = tx_str[STREAM_DATA].dbg_reject;
    out->err_tx_stall    = dbg_err_tx_stall;
    out->err_fifo_full   = dbg_err_fifo_full;
    out->fifo_tx_vacancy = XLlFifo_iTxVacancy(&fifo_npr_tx);
    out->fifo_tx_depth   = g_fifo_data_depth;
    out->rx_count        = rx_str[STREAM_DATA].count;

    out->tlm_ring_size   = tx_str[STREAM_TLM].size;
    out->tlm_count       = tx_str[STREAM_TLM].count;
    out->tlm_count_max   = tx_str[STREAM_TLM].dbg_count_max;
    out->tlm_pkts        = tx_str[STREAM_TLM].dbg_pkts;
    out->tlm_reject      = tx_str[STREAM_TLM].dbg_reject;
    out->tlm_fifo_vacancy = XLlFifo_iTxVacancy(&fifo);
    out->tlm_fifo_depth   = g_fifo_tx_depth;
}

void radio_stream_reset_queue_stats(void) {
    dbg_tx_blocks     = 0;
    dbg_err_tx_stall  = 0;
    dbg_err_fifo_full = 0;
    for (int i = 0; i < STREAM_CNT; i++) {
        tx_str[i].dbg_count_max = 0;
        tx_str[i].dbg_pkts      = 0;
        tx_str[i].dbg_reject    = 0;
    }
}

void radio_stream_reset_rx_ids(void) {
    for (int i = 0; i < STREAM_CNT; i++) {
        rx_str[i].first_block_seen = 0;
        rx_str[i].first_pkt_seen   = 0;
    }
}

void radio_stream_set_loopback(int enable) {    loopback_enabled = enable;
    xil_printf("[RADIO] Loopback %s\r\n", enable ? "ENABLED" : "DISABLED");
}

void radio_stream_get_rx_diag(uint32_t *out5, uint8_t *first16) {
    out5[0] = dbg_rx_len_new;
    out5[1] = dbg_rx_len_legacy;
    out5[2] = dbg_rx_len_other;
    out5[3] = dbg_rx_len_last;
    out5[4] = dbg_rx_magic;
    for (int i = 0; i < 16; i++) first16[i] = dbg_rx_first16[i];
}

int radio_stream_get_loopback(void) {
    return loopback_enabled;
}
