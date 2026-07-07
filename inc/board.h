#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "hw_types.h"
#include "hw_memmap.h"
#include "gpio.h"
#include "hw_i2c.h"
#include "i2c.h"
#include "pin_map.h"
#include "sysctl.h"
#include "systick.h"
#include "interrupt.h"
#include "uart.h"
#include "hw_ints.h"

//=============================================================================
// IO Expander I2C Addresses
//=============================================================================
#define TCA6424_I2CADDR     0x22
#define PCA9557_I2CADDR     0x18

// PCA9557 registers
#define PCA9557_INPUT       0x00
#define PCA9557_OUTPUT      0x01
#define PCA9557_POLINVERT   0x02
#define PCA9557_CONFIG      0x03

// TCA6424 registers
#define TCA6424_CONFIG_PORT0    0x0C
#define TCA6424_CONFIG_PORT1    0x0D
#define TCA6424_CONFIG_PORT2    0x0E
#define TCA6424_INPUT_PORT0     0x00
#define TCA6424_INPUT_PORT1     0x01
#define TCA6424_INPUT_PORT2     0x02
#define TCA6424_OUTPUT_PORT0    0x04
#define TCA6424_OUTPUT_PORT1    0x05
#define TCA6424_OUTPUT_PORT2    0x06

//=============================================================================
// Pin Mappings
//=============================================================================
#define USR_SW1_PERIPH     SYSCTL_PERIPH_GPIOJ
#define USR_SW1_PORT       GPIO_PORTJ_BASE
#define USR_SW1_PIN        GPIO_PIN_0

#define BUZZER_PERIPH       SYSCTL_PERIPH_GPIOK
#define BUZZER_PORT         GPIO_PORTK_BASE
#define BUZZER_PIN          GPIO_PIN_5

#define SW1_PIN             (1 << 0)
#define SW2_PIN             (1 << 1)
#define SW3_PIN             (1 << 2)
#define SW4_PIN             (1 << 3)
#define SW5_PIN             (1 << 4)
#define SW6_PIN             (1 << 5)
#define SW7_PIN             (1 << 6)
#define SW8_PIN             (1 << 7)

#define USER1_PERIPH        SYSCTL_PERIPH_GPIOJ
#define USER1_PORT          GPIO_PORTJ_BASE
#define USER1_PIN           GPIO_PIN_0
#define USER2_PERIPH        SYSCTL_PERIPH_GPIOJ
#define USER2_PORT          GPIO_PORTJ_BASE
#define USER2_PIN           GPIO_PIN_1

#define SW_DEBOUNCE_MS      20

//=============================================================================
// UART Configuration
//=============================================================================
#define UART_BAUD           115200
#define UART_RX_BUF_SIZE    128
#define CMD_BUF_SIZE        64

//=============================================================================
// LED Indicator Assignment (PCA9557, active low: 0=LED on, 1=LED off)
//=============================================================================
#define LED_HB_POS      0
#define LED_ALM_POS     1
#define LED_EDT_POS     2
#define LED_UART_POS    3
#define LED_WX_SUN_POS  4
#define LED_WX_RAIN_POS 5
#define LED_TEMP_POS    6
#define LED_NTP_POS     7

//=============================================================================
// 7-Segment Letter Patterns
//=============================================================================
#define SEG_L   0x3C   // L: c,d,e,f
#define SEG_U   0x3E   // U: b,c,d,e,f
#define SEG_A   0x77   // A: a,b,c,e,f,g
#define SEG_N   0x37   // N: a,b,c,e,f
#define SEG_Y   0x6E   // Y: b,c,d,f,g
#define SEG_M   0x55   // M: a,c,e,g
#define SEG_V   0x7E   // V: b,c,d,e,f,g

//=============================================================================
// Enums
//=============================================================================
typedef enum {
    S_ALL_ON = 0,
    S_ALL_OFF,
    S_NUM_ON,
    S_NUM_OFF,
    S_NAME_ON,
    S_NAME_OFF,
    S_VER,
    S_CLOCK
} state_t;

typedef enum {
    DISP_TIME = 0,
    DISP_DATE_YYMMDD,
    DISP_DATE_YYYYMMDD
} disp_mode_t;

typedef enum {
    EDIT_NONE = 0,
    EDIT_DATE,
    EDIT_TIME,
    EDIT_ALARM
} edit_mode_t;

//=============================================================================
// Extern Global Variables
//=============================================================================
extern volatile uint32_t g_ui32SysTickCount;
extern uint32_t ui32SysClock;

// Clock variables
extern uint8_t hours, minutes, seconds;
extern uint16_t year;
extern uint8_t month, day;

// Display mode
extern disp_mode_t g_disp_mode;

// NTP state
extern uint8_t g_ntp_state;
extern uint8_t g_ntp_blink;
extern uint32_t g_ntp_timer;
extern uint32_t g_ntp_start;
extern uint32_t g_last_ntp_tick;
extern uint8_t  g_last_ntp_h, g_last_ntp_m, g_last_ntp_s;

// Weather state
extern uint8_t g_weather_buf[8];
extern uint8_t g_weather_valid;
extern uint32_t g_weather_until;
extern int8_t g_weather_temp;
extern uint8_t g_weather_cond;
extern uint8_t g_wx_rain_blink;
extern uint32_t g_wx_rain_timer;

// Beep timer
extern uint32_t g_beep_until;

// EVT heartbeat
extern volatile uint8_t g_evt_suppress;
extern uint32_t g_evt_timer;

//=============================================================================
// Function Prototypes (from modules)
//=============================================================================
void Delay(uint32_t value);

// I2C
void S800_I2C0_Init(void);
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);

// GPIO init
void S800_GPIO_Init(void);

// Display
void ScanDisplay(void);
void UpdateClockDisplay(void);
void ScrollInit(const uint8_t *seg, const uint8_t *dp, uint8_t len);
void ScrollRender(void);
void ScrollAdvance(void);

// Buttons
void ReadButtons(void);

// Edit
void EditIncrement(void);

// Date utilities
uint8_t DaysInMonth(uint16_t y, uint8_t m);
void IncrementDate(void);

// LED
void LedUpdate(void);
void SendEvtLed(void);
void SendEvtDisp(void);
void SendEvtKey(const char *name);
void SendEvtEditSave(void);

// Alarm
void AlarmRingingUpdate(uint32_t now);

// UART
void UartInit(void);
void UartSendChar(char c);
void UartSendStr(const char *s);
void UartSendDec(uint32_t v);
void UartSendHexByte(uint8_t v);

// Command processing
void CmdProcess(void);

#endif /* BOARD_H */
