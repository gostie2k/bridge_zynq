// ============================================================
// Тестовая отладка записи в FIFO по символу из UART
// ============================================================
//
// Вставить вызов fifo_debug_uart_test(&Uart1_Ps) в main loop
// ПЕРЕД uart_cmd_chan, чтобы перехватить символ первым.
//
// Команды:
//   'w' — записать один тестовый блок (188 байт) в FIFO
//   'r' — напечатать статус FIFO (vacancy, occupancy, tready)
//   'x' — сброс FIFO (XLlFifo_Reset)
//
// Все остальные символы пропускаются дальше в uart_cmd_chan.

#include "xllfifo.h"
#include "xuartps.h"
#include "xil_printf.h"

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

        if (vacancy < (TEST_BLOCK_SIZE / 4)) {
            xil_printf("[FIFO_TEST] !!! FIFO FULL — vacancy=%u, need %u\r\n",
                       vacancy, TEST_BLOCK_SIZE / 4);
            return;
        }

        // Заполняем тестовый паттерн: seq=test_write_cnt, data=0xAA, 0xBB...
        uint8_t block[TEST_BLOCK_SIZE];
        block[0] = (uint8_t)(test_write_cnt & 0xFF);  // seq
        for (int i = 1; i < TEST_BLOCK_SIZE; i++) {
            block[i] = (uint8_t)(i & 0xFF);  // простой паттерн
        }

        // Записываем в FIFO (little-endian, как в send_block_to_pl)
        u32 word;
        for (int i = 0; i < TEST_BLOCK_SIZE; i += 4) {
            word =  (u32)block[i]         |
                   ((u32)block[i+1] << 8) |
                   ((u32)block[i+2] << 16)|
                   ((u32)block[i+3] << 24);
            XLlFifo_TxPutWord(&fifo, word);
        }
        XLlFifo_iTxSetLen(&fifo, TEST_BLOCK_SIZE);

        test_write_cnt++;

        u32 vacancy_after = XLlFifo_iTxVacancy(&fifo);
        xil_printf("[FIFO_TEST] Write #%u OK. Vacancy after: %u words\r\n",
                   test_write_cnt, vacancy_after);

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

    } else {
        // Не наш символ — положить обратно нельзя,
        // поэтому просто игнорируем. Если нужно сохранить
        // совместимость с uart_cmd_chan, см. комментарии ниже.
        // Вариант: проверять символ ДО чтения из UART,
        // но XUartPs не поддерживает peek.
        //
        // Рабочий вариант: временно отключить uart_cmd_chan
        // на время тестирования.
    }
}
