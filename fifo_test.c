// ============================================================
// Test debug: write to FIFO on a char from UART
// ============================================================
//
// Insert fifo_debug_uart_test(&Uart1_Ps) into the main loop
// BEFORE uart_cmd_chan, to grab the char first.
//
// Commands:
//   'w' - write a test block (TEST_BLOCK_SIZE bytes, padding=0x00)
//   'W' - same, padding=0xEE: compare RS parity with 'w' to check TKEEP
//   'r' - print FIFO status (vacancy, occupancy, tready)
//   'x' - reset FIFO (XLlFifo_Reset)
//   'l' - toggle software loopback (TX -> RX without FPGA)
//   't' - send a test Ethernet frame via radio_linkoutput
//   's' - stress write: 174 blocks x 188 bytes into TX FIFO (non-blocking)
//   'S' - stress read: read back and verify (press after 's' when PL ready)
//
// All other chars are passed on to uart_cmd_chan.

#include "xllfifo.h"
#include "xuartps.h"
#include "xil_printf.h"
//#include "radio_stream.h"

extern XLlFifo fifo;

#define TEST_BLOCK_SIZE 188  // Block size = PL_BLOCK_SIZE

static u32 test_write_cnt = 0;

void fifo_debug_uart_test(XUartPs *uart_ptr)
{
    if (!XUartPs_IsReceiveData(uart_ptr->Config.BaseAddress))
        return;

    u8 ch = (u8)XUartPs_ReadReg(uart_ptr->Config.BaseAddress,
                                 XUARTPS_FIFO_OFFSET);

    if (ch == 'w') {
        // --- Write a test block to FIFO ---
        u32 vacancy = XLlFifo_iTxVacancy(&fifo);

        xil_printf("[FIFO_TEST] Vacancy before write: %u words\r\n", vacancy);

        if (vacancy < ((TEST_BLOCK_SIZE + 3) / 4)) {
            xil_printf("[FIFO_TEST] !!! FIFO FULL — vacancy=%u, need %u\r\n",
                       vacancy, (TEST_BLOCK_SIZE + 3) / 4);
            return;
        }

        // Exactly TEST_BLOCK_SIZE bytes - no padding
        uint8_t block[TEST_BLOCK_SIZE];
        block[0] = (uint8_t)(test_write_cnt & 0xFF);  // seq
        for (int i = 1; i < TEST_BLOCK_SIZE; i++) {
            block[i] = (uint8_t)(i & 0xFF);
        }

        // Write whole words; the last word may be partial -
        // XLlFifo_iTxSetLen tells the encoder the exact byte count
        u32 word;
        u32 words = (TEST_BLOCK_SIZE + 3) / 4;
        for (u32 i = 0; i < words; i++) {
            u32 base = i * 4;
            word  =  (u32)(block[base]);
            word |= (base+1 < TEST_BLOCK_SIZE) ? ((u32)block[base+1] <<  8) : 0;
            word |= (base+2 < TEST_BLOCK_SIZE) ? ((u32)block[base+2] << 16) : 0;
            word |= (base+3 < TEST_BLOCK_SIZE) ? ((u32)block[base+3] << 24) : 0;
            XLlFifo_TxPutWord(&fifo, word);
        }
        XLlFifo_iTxSetLen(&fifo, TEST_BLOCK_SIZE);

        test_write_cnt++;

        u32 vacancy_after = XLlFifo_iTxVacancy(&fifo);
        xil_printf("[FIFO_TEST] Write #%u OK. Vacancy after: %u words\r\n",
                   test_write_cnt, vacancy_after);

    } else if (ch == 'W') {
        // --- Write a block with 0xEE in padding bytes of the last word ---
        // Used to check TKEEP: if RS parity matches 'w',
        // the encoder ignores the extra bytes (TKEEP works).
        // If parity differs - encoder processes 188 bytes instead of TEST_BLOCK_SIZE.
        u32 vacancy = XLlFifo_iTxVacancy(&fifo);
        xil_printf("[FIFO_TEST] [W] Vacancy before write: %u words\r\n", vacancy);
        if (vacancy < ((TEST_BLOCK_SIZE + 3) / 4)) {
            xil_printf("[FIFO_TEST] !!! FIFO FULL\r\n");
            return;
        }
        uint8_t block[TEST_BLOCK_SIZE];
        block[0] = (uint8_t)(test_write_cnt & 0xFF);
        for (int i = 1; i < TEST_BLOCK_SIZE; i++)
            block[i] = (uint8_t)(i & 0xFF);

        u32 word;
        u32 words = (TEST_BLOCK_SIZE + 3) / 4;
        for (u32 i = 0; i < words; i++) {
            u32 base = i * 4;
            word  =  (u32)(block[base]);
            // Fill padding bytes with 0xEE (not 0x00)
            word |= (base+1 < (u32)TEST_BLOCK_SIZE) ? ((u32)block[base+1] <<  8) : (0xEEu <<  8);
            word |= (base+2 < (u32)TEST_BLOCK_SIZE) ? ((u32)block[base+2] << 16) : (0xEEu << 16);
            word |= (base+3 < (u32)TEST_BLOCK_SIZE) ? ((u32)block[base+3] << 24) : (0xEEu << 24);
            XLlFifo_TxPutWord(&fifo, word);
        }
        XLlFifo_iTxSetLen(&fifo, TEST_BLOCK_SIZE);
        test_write_cnt++;
        xil_printf("[FIFO_TEST] [W] Write #%u OK (0xEE filler). Compare RS parity with 'w'.\r\n",
                   test_write_cnt);

    } else if (ch == 's') {
        // --- Stress test: write 174 blocks of 188 bytes ---
        // Write is non-blocking. After - press 'r' while RX Occupancy grows,
        // then 'S' to read and verify.
        xil_printf("[STRESS] Start: 174 blocks x 188 bytes\r\n");

        // Reset FIFO before the test - otherwise RX may hold
        // leftovers from a previous run and the seq check gives false errors
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
        XLlFifo_Reset(&fifo);
        xil_printf("[STRESS] FIFO reset done.\r\n");

        int stress_write_ok = 0;
        for (int b = 0; b < 174; b++) {
            if (XLlFifo_iTxVacancy(&fifo) < (188 / 4)) {
                xil_printf("[STRESS] TX FULL at block %d\r\n", b);
                break;
            }
            u32 sword;
            for (int i = 0; i < 188; i += 4) {
                uint8_t b0 = (i == 0) ? (uint8_t)(b & 0xFF) : (uint8_t)(i & 0xFF);
                sword = (u32)b0
                      | ((u32)(uint8_t)((i+1) & 0xFF) <<  8)
                      | ((u32)(uint8_t)((i+2) & 0xFF) << 16)
                      | ((u32)(uint8_t)((i+3) & 0xFF) << 24);
                XLlFifo_TxPutWord(&fifo, sword);
            }
            XLlFifo_iTxSetLen(&fifo, 188);
            stress_write_ok++;
        }
        xil_printf("[STRESS] Written: %d/174\r\n", stress_write_ok);
        xil_printf("[STRESS] RX Occupancy now: %u words\r\n", XLlFifo_iRxOccupancy(&fifo));
        xil_printf("[STRESS] Press 'S' when PL done (check 'r').\r\n");

    } else if (ch == 'S') {
        // --- Read back and verify stress-test results ---
        xil_printf("[STRESS] Reading back...\r\n");
        xil_printf("[STRESS] RX Occupancy: %u words\r\n", XLlFifo_iRxOccupancy(&fifo));
        int sread_ok = 0, sread_err = 0, sparity = 0, sblk = 0;
        while (XLlFifo_iRxOccupancy(&fifo) > 0) {
            u32 pkt_len = XLlFifo_iRxGetLen(&fifo);
            if (pkt_len < 188 || pkt_len > 512) {
                xil_printf("[STRESS] Bad len=%u at block %d\r\n", pkt_len, sblk);
                u32 skip = (pkt_len + 3) / 4;
                for (u32 i = 0; i < skip; i++) XLlFifo_RxGetWord(&fifo);
                sread_err++; sblk++; continue;
            }
            uint8_t srx[512];
            u32 swords = (pkt_len + 3) / 4;
            for (u32 i = 0; i < swords; i++) {
                u32 sw = XLlFifo_RxGetWord(&fifo);
                srx[i*4+0] = (uint8_t)(sw        & 0xFF);
                srx[i*4+1] = (uint8_t)((sw >>  8) & 0xFF);
                srx[i*4+2] = (uint8_t)((sw >> 16) & 0xFF);
                srx[i*4+3] = (uint8_t)((sw >> 24) & 0xFF);
            }
            int serr = 0;
            if (srx[0] != (uint8_t)(sblk & 0xFF)) serr++;
            for (int i = 1; i < 188; i++)
                if (srx[i] != (uint8_t)(i & 0xFF)) serr++;
            if (serr > 0) {
                xil_printf("[STRESS] Block %d: %d errors (seq got=0x%02X exp=0x%02X)\r\n",
                           sblk, serr, srx[0], (uint8_t)(sblk & 0xFF));
                sread_err++;
            } else {
                sread_ok++;
            }
            sparity += (int)(pkt_len - 188);
            sblk++;
        }
        xil_printf("[STRESS] === RESULT ===\r\n");
        xil_printf("  Total read   : %d\r\n", sblk);
        xil_printf("  Read OK      : %d\r\n", sread_ok);
        xil_printf("  Errors       : %d\r\n", sread_err);
        xil_printf("  RS parity/blk: %d\r\n", sblk > 0 ? sparity / sblk : 0);
        xil_printf("  %s\r\n", sread_err == 0 ? "PASS" : "FAIL");

        } else if (ch == 'r') {
        // --- FIFO status ---
        u32 vacancy   = XLlFifo_iTxVacancy(&fifo);
        u32 occupancy = XLlFifo_iRxOccupancy(&fifo);
        u32 isr       = XLlFifo_IntPending(&fifo);

        xil_printf("[FIFO_TEST] === FIFO STATUS ===\r\n");
        xil_printf("  TX Vacancy  : %u words\r\n", vacancy);
        xil_printf("  RX Occupancy: %u words\r\n", occupancy);
        xil_printf("  ISR pending : 0x%08X\r\n", isr);
        xil_printf("  Writes done : %u\r\n", test_write_cnt);

        // Check: if vacancy does not change between writes -
        // the PL side (temp_0) is not reading data
        if (vacancy == 0) {
            xil_printf("  >>> FIFO STUCK: PL not reading (tready=0)!\r\n");
        }

    } else if (ch == 'x') {
        // --- FIFO reset ---
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
        XLlFifo_Reset(&fifo);
        test_write_cnt = 0;
        xil_printf("[FIFO_TEST] FIFO RESET done.\r\n");


    } else if (ch == 'c') {
        // --- Reset radio_stream stats and state ---
        radio_stream_reset_stats();
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
        XLlFifo_Reset(&fifo);
        xil_printf("[FIFO_TEST] Stats cleared, FIFO reset.\r\n");

    } else if (ch == 'p') {
        // --- Read one block from RX FIFO, full dump ---
        u32 occupancy = XLlFifo_iRxOccupancy(&fifo);
        xil_printf("[FIFO_TEST] RX Occupancy: %u words\r\n", occupancy);
        if (occupancy == 0) {
            xil_printf("[FIFO_TEST] RX FIFO empty\r\n");
            return;
        }
        u32 pkt_len = XLlFifo_iRxGetLen(&fifo);
        xil_printf("[FIFO_TEST] RX packet length: %u bytes\r\n", pkt_len);
        if (pkt_len == 0 || pkt_len > 512) {
            xil_printf("[FIFO_TEST] Bad packet length %u — skip\r\n", pkt_len);
            return;
        }
        uint8_t rx_block[512];
        u32 words = (pkt_len + 3) / 4;
        for (u32 i = 0; i < words; i++) {
            u32 word = XLlFifo_RxGetWord(&fifo);
            rx_block[i*4+0] = (uint8_t)(word        & 0xFF);
            rx_block[i*4+1] = (uint8_t)((word >> 8)  & 0xFF);
            rx_block[i*4+2] = (uint8_t)((word >> 16) & 0xFF);
            rx_block[i*4+3] = (uint8_t)((word >> 24) & 0xFF);
        }
        xil_printf("[FIFO_TEST] Full dump (%u bytes):\r\n", pkt_len);
        for (u32 i = 0; i < pkt_len; i++) {
            xil_printf("%02X ", rx_block[i]);
            if ((i + 1) % 16 == 0)
                xil_printf("  | %u-%u\r\n", i - 15, i);
        }
        if (pkt_len % 16 != 0) {
            u32 rem = pkt_len % 16;
            for (u32 i = rem; i < 16; i++) xil_printf("   ");
            xil_printf("  | %u-%u\r\n", pkt_len - rem, pkt_len - 1);
        }
        // Pattern check: data up to TEST_BLOCK_SIZE, padding = 0x00
        int errors = 0;
        for (u32 i = 1; i < 188 && i < pkt_len; i++) {
            uint8_t expected = (i < (u32)TEST_BLOCK_SIZE) ? (uint8_t)(i & 0xFF) : 0x00;
            if (rx_block[i] != expected) errors++;
        }
        u32 extra = (pkt_len > 188) ? (pkt_len - 188) : 0;
        xil_printf("[FIFO_TEST] seq=%u  data_errors=%d  rs_parity_bytes=%u\r\n",
                   rx_block[0], errors, extra);

    } else {
        // so just ignore it. If you need to keep
        // compatibility with uart_cmd_chan, see comments below.
        // Option: check the char BEFORE reading from UART,
        // but XUartPs does not support peek.
        //
        // Working option: temporarily disable uart_cmd_chan
        // during testing.
    }
}
