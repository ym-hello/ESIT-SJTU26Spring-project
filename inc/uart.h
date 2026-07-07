#ifndef UART_H
#define UART_H
#include "board.h"

extern volatile uint8_t g_uart_rx_buf[UART_RX_BUF_SIZE];
extern volatile uint8_t g_uart_rx_head;
extern volatile uint8_t g_uart_rx_tail;
extern uint32_t g_tx_led_timer;
extern uint32_t g_rx_led_timer;
#endif
