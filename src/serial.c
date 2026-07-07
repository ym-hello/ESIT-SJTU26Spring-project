// UART0 communication for S800 clock
#include "serial.h"

volatile uint8_t g_uart_rx_buf[UART_RX_BUF_SIZE];
volatile uint8_t g_uart_rx_head = 0;
volatile uint8_t g_uart_rx_tail = 0;
uint32_t g_tx_led_timer = 0;
uint32_t g_rx_led_timer = 0;

void UartInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));

    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSetExpClk(UART0_BASE, ui32SysClock, UART_BAUD,
        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);

    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX1_8, UART_FIFO_RX1_8);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
    UARTEnable(UART0_BASE);
    IntEnable(INT_UART0);
}

void UART0_Handler(void)
{
    uint32_t status = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, status);

    while (UARTCharsAvail(UART0_BASE))
    {
        char c = (char)UARTCharGetNonBlocking(UART0_BASE);
        uint8_t next = (g_uart_rx_head + 1) % UART_RX_BUF_SIZE;
        if (next != g_uart_rx_tail) {
            g_uart_rx_buf[g_uart_rx_head] = (uint8_t)c;
            g_uart_rx_head = next;
        }
        g_rx_led_timer = g_ui32SysTickCount;
    }
}

void UartSendChar(char c)
{
    UARTCharPut(UART0_BASE, (uint8_t)c);
    g_tx_led_timer = g_ui32SysTickCount;
}

void UartSendStr(const char *s)
{
    while (*s) UartSendChar(*s++);
}

void UartSendDec(uint32_t v)
{
    char buf[12];
    uint8_t i = 0;
    if (v == 0) { UartSendChar('0'); return; }
    while (v > 0 && i < sizeof(buf) - 1) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) UartSendChar(buf[--i]);
}

void UartSendHexByte(uint8_t v)
{
    const char hex[] = "0123456789ABCDEF";
    UartSendChar(hex[v >> 4]);
    UartSendChar(hex[v & 0x0F]);
}
