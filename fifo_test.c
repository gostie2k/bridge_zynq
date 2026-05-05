// ============================================================
// Тестовая отладка записи в FIFO по символу из UART
// ============================================================
//
// Вставить вызов fifo_debug_uart_test(&Uart1_Ps) в main loop
// ПЕРЕД uart_cmd_chan, чтобы перехватить символ первым.
//
// Команды:
//   'w' — записать тестовый блок (TEST_BLOCK_SIZE байт, padding=0x00)
//   'W' — то же, но padding=0xEE: сравни RS-паритет с 'w' для проверки TKEEP
//   'r' — напечатать статус FIFO (vacancy, occupancy, tready)
//   'x' — сброс FIFO (XLlFifo_Reset)
//   'l' — переключить software loopback (TX -> RX без FPGA)
//   't' — отправить тестовый Ethernet-фрейм через radio_linkoutput
//   's' — стресс-тест запись: 174 блока × 188 байт в TX FIFO (не блокирует)
//   'S' — стресс-тест чтение: вычитать и проверить (жать после 's' когда PL готов)
//
// Все остальные символы пропускаются дальше в uart_cmd_chan.

#include "xllfifo.h"
#include "xuartps.h"
#include "xil_printf.h"
//#include "radio_stream.h"

extern XLlFifo fifo;

#define TEST_BLOCK_SIZE 188  // Размер блока = PL_BLOCK_SIZE

static u32 test_write_cnt = 0;

void fifo_debug_uart_test(XUartPs *uart_ptr)
{
    if (!XUartPs_IsReceiveData(uart_ptr->Config.BaseAddress))
        return;

    u8 ch = (u8)XUartPs_ReadReg(uart_ptr->Config.BaseAddress,
                                 XUARTPS_FIFO_OFFSET);

    if (ch == 'w') {
        // --- Записать тестовый блок в FIFO ---
        u32 vacancy = XLlFifo_iTxVacancy(&fifo);

        xil_printf("[FIFO_TEST] Vacancy before write: %u words\r\n", vacancy);

        if (vacancy < ((TEST_BLOCK_SIZE + 3) / 4)) {
            xil_printf("[FIFO_TEST] !!! FIFO FULL — vacancy=%u, need %u\r\n",
                       vacancy, (TEST_BLOCK_SIZE + 3) / 4);
            return;
        }

        // Ровно TEST_BLOCK_SIZE байт — без padding
        uint8_t block[TEST_BLOCK_SIZE];
        block[0] = (uint8_t)(test_write_cnt & 0xFF);  // seq
        for (int i = 1; i < TEST_BLOCK_SIZE; i++) {
            block[i] = (uint8_t)(i & 0xFF);
        }

        // Записываем целые слова; последнее слово может быть неполным —
        // XLlFifo_iTxSetLen укажет энкодеру точное число байт
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
        // --- Записать блок с 0xEE в padding-байтах последнего слова ---
        // Используется для проверки TKEEP: если RS-паритет совпадает с 'w',
        // значит энкодер игнорирует лишние байты (TKEEP работает).
        // Если паритет отличается — энкодер обрабатывает 188 байт вместо TEST_BLOCK_SIZE.
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
            // Padding байты заполняем 0xEE (а не 0x00)
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
        // --- Стресс-тест: записать 174 блока по 188 байт ---
        // Запись не блокирует. После — жми 'r' пока RX Occupancy растёт,
        // затем 'S' для чтения и проверки.
        xil_printf("[STRESS] Start: 174 blocks x 188 bytes\r\n");

        // Сбросить FIFO перед тестом — иначе в RX могут лежать
        // остатки от предыдущего запуска и проверка seq даст ложные ошибки
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
        // --- Вычитать и проверить результаты стресс-теста ---
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
        // --- Статус FIFO ---
        u32 vacancy   = XLlFifo_iTxVacancy(&fifo);
        u32 occupancy = XLlFifo_iRxOccupancy(&fifo);
        u32 isr       = XLlFifo_IntPending(&fifo);

        xil_printf("[FIFO_TEST] === FIFO STATUS ===\r\n");
        xil_printf("  TX Vacancy  : %u words\r\n", vacancy);
        xil_printf("  RX Occupancy: %u words\r\n", occupancy);
        xil_printf("  ISR pending : 0x%08X\r\n", isr);
        xil_printf("  Writes done : %u\r\n", test_write_cnt);

        // Проверка: если vacancy не меняется между записями —
        // значит PL-сторона (temp_0) не читает данные
        if (vacancy == 0) {
            xil_printf("  >>> FIFO STUCK: PL не читает (tready=0)!\r\n");
        }

    } else if (ch == 'x') {
        // --- Сброс FIFO ---
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
        XLlFifo_Reset(&fifo);
        test_write_cnt = 0;
        xil_printf("[FIFO_TEST] FIFO RESET done.\r\n");

//    } else if (ch == 'l') {
//        // --- Переключить loopback ---
//        int now = !radio_stream_get_loopback();
//        radio_stream_set_loopback(now);
//
//    } else if (ch == 't') {
//        // --- Тест сегментации: отправить тестовый Ethernet-фрейм через radio_linkoutput ---
//        // Минимальный Ethernet-фрейм: dst(6) + src(6) + type(2) + payload(46) = 60 байт
//        static uint8_t test_eth[60];
//        static uint8_t test_seq = 0;
//
//        // dst = broadcast, src = 00:0A:35:AA:BB:CC, type = 0x0800 (IP-like)
//        memset(test_eth, 0xFF, 6);                              // dst broadcast
//        test_eth[6]=0x00; test_eth[7]=0x0A; test_eth[8]=0x35;
//        test_eth[9]=0xAA; test_eth[10]=0xBB; test_eth[11]=0xCC; // src
//        test_eth[12]=0x08; test_eth[13]=0x00;                   // EtherType
//        for (int i=14; i<60; i++) test_eth[i] = (uint8_t)(test_seq + i); // payload
//        test_seq++;
//
//        // Завернуть в pbuf и отправить через radio_linkoutput
//        extern struct netif port3_bridge;
//        struct pbuf *tp = pbuf_alloc(PBUF_RAW, 60, PBUF_RAM);
//        if (tp) {
//            memcpy(tp->payload, test_eth, 60);
//            xil_printf("[FIFO_TEST] Sending test Eth frame #%u (60 bytes) via radio\r\n",
//                       (unsigned)(test_seq-1));
//            err_t e = radio_linkoutput(&port3_bridge, tp);
//            pbuf_free(tp);
//            xil_printf("[FIFO_TEST] radio_linkoutput returned %d\r\n", (int)e);
//            radio_stream_print_stats();
//        } else {
//            xil_printf("[FIFO_TEST] pbuf_alloc failed\r\n");
//        }
    } else if (ch == 'c') {
        // --- Сброс статистики и состояния radio_stream ---
        radio_stream_reset_stats();
        XLlFifo_IntClear(&fifo, 0xFFFFFFFF);
        XLlFifo_Reset(&fifo);
        xil_printf("[FIFO_TEST] Stats cleared, FIFO reset.\r\n");

    } else if (ch == 'p') {
        // --- Вычитать один блок из RX FIFO, полный дамп ---
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
        // Проверка паттерна: данные до TEST_BLOCK_SIZE, padding = 0x00
        int errors = 0;
        for (u32 i = 1; i < 188 && i < pkt_len; i++) {
            uint8_t expected = (i < (u32)TEST_BLOCK_SIZE) ? (uint8_t)(i & 0xFF) : 0x00;
            if (rx_block[i] != expected) errors++;
        }
        u32 extra = (pkt_len > 188) ? (pkt_len - 188) : 0;
        xil_printf("[FIFO_TEST] seq=%u  data_errors=%d  rs_parity_bytes=%u\r\n",
                   rx_block[0], errors, extra);

    } else {
        // поэтому просто игнорируем. Если нужно сохранить
        // совместимость с uart_cmd_chan, см. комментарии ниже.
        // Вариант: проверять символ ДО чтения из UART,
        // но XUartPs не поддерживает peek.
        //
        // Рабочий вариант: временно отключить uart_cmd_chan
        // на время тестирования.
    }
}
