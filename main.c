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

// IO expander addresses
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

// Button (unused in this demo, kept for completeness)
#define USR_SW1_PERIPH     SYSCTL_PERIPH_GPIOJ
#define USR_SW1_PORT       GPIO_PORTJ_BASE
#define USR_SW1_PIN        GPIO_PIN_0

// Buzzer on PK5 (active high, via ULN2003 inverts to drive)
#define BUZZER_PERIPH       SYSCTL_PERIPH_GPIOK
#define BUZZER_PORT         GPIO_PORTK_BASE
#define BUZZER_PIN          GPIO_PIN_5

// SW1 / K1 (FUNC button) on P01 = bit 0
#define SW1_PIN             (1 << 0)
// SW2 / K2 (SHIFT button) on P02 = bit 1
#define SW2_PIN             (1 << 1)
// SW3 / K3 (ADD button) on P03 = bit 2
#define SW3_PIN             (1 << 2)
// SW4 / K4 (SAVE button) on P04 = bit 3
#define SW4_PIN             (1 << 3)
// SW5 / K5 (DISP button) on P05 = bit 4
#define SW5_PIN             (1 << 4)
// SW6 / K6 (SPEED button) on P06 = bit 5
#define SW6_PIN             (1 << 5)
// SW7 / K7 (FORMAT button) on P07 = bit 6
#define SW7_PIN             (1 << 6)
// SW8 / K8 (EXT button) on P07 = bit 7
#define SW8_PIN             (1 << 7)

// USER1 / USER2 GPIO buttons (PJ0, PJ1, active low with pull-up)
#define USER1_PERIPH        SYSCTL_PERIPH_GPIOJ
#define USER1_PORT          GPIO_PORTJ_BASE
#define USER1_PIN           GPIO_PIN_0
#define USER2_PERIPH        SYSCTL_PERIPH_GPIOJ
#define USER2_PORT          GPIO_PORTJ_BASE
#define USER2_PIN           GPIO_PIN_1

#define SW_DEBOUNCE_MS      20

// UART configuration
#define UART_BAUD           115200
#define UART_RX_BUF_SIZE    128
#define CMD_BUF_SIZE        64

//=============================================================================
// LED Indicator Assignment (PCA9557, active low: 0=LED on, 1=LED off)
//=============================================================================
// Bit  Label  Meaning               Behavior
// ---  -----  --------------------  ----------------------------
//  0   HB     System heartbeat      1Hz (500ms on/off)
//  1   ALM    Alarm armed/ringing   Steady when armed; fast blink when ringing
//  2   EDT    Edit mode             Steady on when editing
//  3   UART   TX/RX activity        On for 100ms after activity
//  4   WX_SUN Sunny weather         Steady on/off (reserved)
//  5   WX_RAIN Rain/snow weather    1Hz breathing (reserved)
//  6   TEMP   High temp ≥30°C       Steady on (reserved)
//  7   NTP    NTP time sync         Steady=synced, 1Hz=syncing, off=idle
//=============================================================================
#define LED_HB_POS      0
#define LED_ALM_POS     1
#define LED_EDT_POS     2
#define LED_UART_POS    3
#define LED_WX_SUN_POS  4
#define LED_WX_RAIN_POS 5
#define LED_TEMP_POS    6
#define LED_NTP_POS     7

// Function prototypes
void Delay(uint32_t value);
void S800_GPIO_Init(void);
void S800_I2C0_Init(void);
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);
void ScanDisplay(void);
void StateMachineUpdate(void);
void UpdateClockDisplay(void);
void ScrollInit(const uint8_t *seg, const uint8_t *dp, uint8_t len);
void ScrollRender(void);
void ScrollAdvance(void);
void ReadButtons(void);
void EditIncrement(void);
uint8_t DaysInMonth(uint16_t y, uint8_t m);
void IncrementDate(void);
static void LedUpdate(void);
static void SendEvtDisp(void);
static void SendEvtLed(void);
static void SendEvtKey(const char *name);
static void SendEvtEditSave(void);
static void UartInit(void);
static void UartSendChar(char c);
static void UartSendStr(const char *s);
static void UartSendDec(uint32_t v);
static void UartSendHexByte(uint8_t v);
static void CmdProcess(void);

// 7-segment pattern for 0-9, A-F (common cathode, a=bit0, b=bit1, ..., g=bit6, dp=bit7)
const uint8_t seg7[] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F, // 9
    0x77, // A
    0x7C, // b
    0x58, // c
    0x5E, // d
    0x79, // E
    0x71  // F
};

// Custom 7-segment patterns for letters
#define SEG_L   0x3C   // L: c,d,e,f
#define SEG_U   0x3E   // U: b,c,d,e,f 
#define SEG_A   0x77   // A: a,b,c,e,f,g
#define SEG_N   0x37   // N: a,b,c,e,f
#define SEG_Y   0x6E   // Y: b,c,d,f,g
#define SEG_M   0x55   // M: a,c,e,g
#define SEG_V   0x7E   // V: b,c,d,e,f,g

// Global variables
volatile uint32_t g_ui32SysTickCount = 0;
uint32_t ui32SysClock;
uint8_t g_disp_buf[8];         // segment values for 8 digits
uint8_t g_led_state;          // PCA9557 output (0 = LED on, 1 = off)

// State machine
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

state_t g_state = S_ALL_ON;
uint32_t g_state_start_tick;
int g_all_blink_count = 0;

// Clock variables
uint8_t hours = 0;
uint8_t minutes = 0;
uint8_t seconds = 0;

// Date variables
uint16_t year = 2026;
uint8_t month = 6;
uint8_t day = 4;

// Display mode
typedef enum {
    DISP_TIME = 0,
    DISP_DATE_YYMMDD,
    DISP_DATE_YYYYMMDD
} disp_mode_t;
disp_mode_t g_disp_mode = DISP_TIME;


// Sets g_scroll_dir when present: LEFT=0, RIGHT=1.

// SW1 (FUNC), SW2 (SHIFT), SW3 (ADD) button states
uint8_t g_sw1_pressed = 0, g_sw1_edge = 0;
uint8_t g_sw2_pressed = 0, g_sw2_edge = 0;
uint8_t g_sw3_pressed = 0, g_sw3_edge = 0;
// SW4 (SAVE) button state
uint8_t g_sw4_pressed = 0;
uint8_t g_sw4_edge = 0;
// SW5 button state (debounced)
uint8_t g_sw5_pressed = 0;
uint8_t g_sw5_edge = 0;
// SW6 (SPEED) button state
uint8_t g_sw6_pressed = 0;
uint8_t g_sw6_edge = 0;
// SW7 (FORMAT) button state
uint8_t g_sw7_pressed = 0;
uint8_t g_sw7_edge = 0;
// SW8 (EXT) button state
uint8_t g_sw8_pressed = 0;
uint8_t g_sw8_edge = 0;
// USER1 / USER2 GPIO button state
uint8_t g_user1_pressed = 0;
uint8_t g_user1_edge = 0;
uint8_t g_user2_pressed = 0;
uint8_t g_user2_edge = 0;

// Scroll mode variables (reusable for any content exceeding 8 digits)
const uint8_t *g_pscroll_seg = 0;   // pointer to segment data to scroll
const uint8_t *g_pscroll_dp = 0;    // pointer to DP data to scroll
uint8_t g_scroll_len = 0;           // data length (gap of 2 blanks appended virtually)
uint8_t g_scroll_active = 0;        // 0=normal clock, 1=scroll mode
uint8_t g_scroll_pos = 0;           // window start position (continuous ring)
uint8_t g_scroll_dir = 0;           // 0=LEFT→RIGHT, 1=RIGHT→LEFT
uint8_t g_scroll_speed = 0;         // 0=slow(300ms), 1=fast(100ms)
uint32_t g_scroll_timer = 0;
uint8_t g_scroll_auto_exit = 0;     // 1 = auto-exit after one full cycle
uint8_t g_scroll_cycles = 0;        // complete cycle count for auto-exit
uint32_t g_msg_static_until = 0;     // timer for ≤8 char static message exit

// Edit mode state machine
typedef enum {
    EDIT_NONE = 0,
    EDIT_DATE,
    EDIT_TIME,
    EDIT_ALARM
} edit_mode_t;
uint8_t g_edit_mode = EDIT_NONE;
uint8_t g_edit_field = 0;
uint8_t g_blink_show = 1;
uint32_t g_blink_timer = 0;
uint32_t g_edit_timeout = 0;
uint32_t g_func_press_tick = 0;
uint8_t g_func_hold_triggered = 0;
uint8_t g_func_entered = 0;         // 1 = just entered from NONE this press

// ADD (SW3) auto-repeat state
uint32_t g_add_repeat_next = 0;

// LED heartbeat state
uint8_t g_led_hb = 0;
uint32_t g_hb_timer = 0;
uint8_t g_led_override = 0;    // 1 = PC directly controls LEDs via *SET:LED

// 1-second EVT heartbeat timer
uint32_t g_evt_timer = 0;
static volatile uint8_t g_evt_suppress = 0;

// Weather display (set by PC via *SET:WEATHER, shown on USER2 press)
uint8_t g_weather_buf[8];
uint8_t g_weather_valid = 0;
uint32_t g_weather_until = 0;
int8_t g_weather_temp = 0;     // temperature in °C (-99..99)
uint8_t g_weather_cond = 0;    // 0=unknown, 1=sunny, 2=cloudy, 3=rainy, 4=snowy
uint8_t g_wx_rain_blink = 0;
uint32_t g_wx_rain_timer = 0;

// NTP time sync: state 1=syncing (blink), tracked for LED accuracy
uint8_t g_ntp_state = 0;
uint8_t g_ntp_blink = 0;
uint32_t g_ntp_timer = 0;
uint32_t g_ntp_start = 0;
// Last NTP sync tracking: tick and time values at sync moment
uint32_t g_last_ntp_tick = 0;
uint8_t  g_last_ntp_h = 0, g_last_ntp_m = 0, g_last_ntp_s = 0;

// Alarm
uint8_t alarm_hours = 0;
uint8_t alarm_minutes = 0;
uint8_t alarm_seconds = 0;
uint8_t alarm_enabled = 0;       // 0=disabled, 1=armed
uint8_t alarm_ringing = 0;       // 1=currently ringing
uint32_t alarm_ring_start = 0;   // SysTick when ringing started
uint8_t alarm_buzzer_on = 0;     // buzzer rhythm state
uint32_t alarm_buzzer_timer = 0; // buzzer rhythm timer
uint32_t alarm_led_timer = 0;    // LED flash timer for ringing
uint8_t alarm_extra_beeps = 0;   // extra beeps for rain/snow (6 half-cycles = 3 beeps)
uint32_t alarm_extra_timer = 0;  // timer for extra beep phase
uint8_t alarm_hitemp = 0;        // 1 = high temp slow flash during alarm
uint8_t alarm_extra_phase = 0;   // 1 = in extra beep phase (after normal 10s)

// Saved originals for cancel-on-timeout
static uint16_t sv_year;
static uint8_t sv_month, sv_day;
static uint8_t sv_hours, sv_minutes, sv_seconds;
static uint8_t sv_alarm_h, sv_alarm_m, sv_alarm_s;
static uint8_t sv_disp_mode;    // original display format (restore on exit)

// UART ring buffer and serial state
static volatile uint8_t g_uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint8_t g_uart_rx_head = 0;
static volatile uint8_t g_uart_rx_tail = 0;
uint32_t g_tx_led_timer = 0;
uint32_t g_rx_led_timer = 0;
uint32_t g_beep_until = 0;
uint8_t g_disp_off = 0;        // 1 = display blanked (*SET:DISPlay OFF)
uint8_t g_mode_night = 0;      // 0=DAY, 1=NIGHT (*SET:MODE)

int main(void)
{
    // Set system clock to 16 MHz
    ui32SysClock = SysCtlClockFreqSet((SYSCTL_XTAL_16MHZ | SYSCTL_OSC_INT | SYSCTL_USE_OSC), 16000000);

    // Initialize GPIO, I2C, and UART
    S800_GPIO_Init();
    S800_I2C0_Init();
    UartInit();

    // Configure SysTick for 1 ms intervals
    SysTickPeriodSet(ui32SysClock / 1000 - 1);
    SysTickIntEnable();
    SysTickEnable();

    // Enable interrupts globally
    IntMasterEnable();

    // Initial display state: all segments on, all LEDs on
    memset(g_disp_buf, 0xFF, 8);   // all segments (and dp) on
    g_led_state = 0x00;            // LED on (active low)
    g_state = S_ALL_ON;
    g_state_start_tick = g_ui32SysTickCount;
    g_all_blink_count = 0;

    // Main loop: display scan and state machine driven by SysTick
    while (1)
    {
        static uint32_t last_tick = 0;
        uint32_t current_tick = g_ui32SysTickCount;

        if (current_tick != last_tick)
        {
            last_tick = current_tick;
            ReadButtons();           // update button states first
            ScanDisplay();           // multiplex one digit per tick
            StateMachineUpdate();    // check timers, buttons, and change state
            CmdProcess();            // process incoming serial commands
        }
    }
}

//---------------------------------------------------------------------------
// SysTick interrupt handler
//---------------------------------------------------------------------------
void SysTick_Handler(void)
{
    g_ui32SysTickCount++;
}

//---------------------------------------------------------------------------
// Scan one digit of the display and update LEDs
//---------------------------------------------------------------------------
void ScanDisplay(void)
{
    static uint8_t idx = 0;

    // Turn off all digits to avoid ghosting
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);

    // Write segment value for current digit
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, g_disp_buf[idx]);

    // Enable the current digit
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, (uint8_t)(1 << idx));

    // Update LEDs
    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, g_led_state);

    // Move to next digit (8 digits total)
    idx = (idx + 1) & 0x07;
}

//---------------------------------------------------------------------------
// State machine: handles sequence of blinking and final clock
//---------------------------------------------------------------------------
void StateMachineUpdate(void)
{
    uint32_t now = g_ui32SysTickCount;
    uint32_t elapsed = now - g_state_start_tick;

    switch (g_state)
    {
    case S_ALL_ON:
        if (elapsed >= 500)   // 500 ms on
        {
            // Switch to all off
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;        // LED off
            g_state = S_ALL_OFF;
            g_state_start_tick = now;
            g_all_blink_count++;
        }
        break;

    case S_ALL_OFF:
        if (elapsed >= 500)   // 500 ms off
        {
            if (g_all_blink_count >= 2)  // blinked at least 2 times
            {
                // Show student ID: 31910542
                g_disp_buf[0] = seg7[3];
                g_disp_buf[1] = seg7[1];
                g_disp_buf[2] = seg7[9];
                g_disp_buf[3] = seg7[1];
                g_disp_buf[4] = seg7[0];
                g_disp_buf[5] = seg7[5];
                g_disp_buf[6] = seg7[4];
                g_disp_buf[7] = seg7[2];
                g_led_state = 0x00;    // LEDs on
                g_state = S_NUM_ON;
            }
            else
            {
                // Another blink cycle
                memset(g_disp_buf, 0xFF, 8);
                g_led_state = 0x00;
                g_state = S_ALL_ON;
            }
            g_state_start_tick = now;
        }
        break;

    case S_NUM_ON:
        if (elapsed >= 500)
        {
            // Blink off
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;
            g_state = S_NUM_OFF;
            g_state_start_tick = now;
        }
        break;

    case S_NUM_OFF:
        if (elapsed >= 500)
        {
            // Show name: LUANYM
            g_disp_buf[0] = SEG_L;
            g_disp_buf[1] = SEG_U;
            g_disp_buf[2] = SEG_A;
            g_disp_buf[3] = SEG_N;
            g_disp_buf[4] = SEG_Y;
            g_disp_buf[5] = SEG_M;
            g_disp_buf[6] = 0x00;     // blank
            g_disp_buf[7] = 0x00;
            g_led_state = 0x00;       // LEDs on
            g_state = S_NAME_ON;
            g_state_start_tick = now;
        }
        break;

    case S_NAME_ON:
        if (elapsed >= 500)
        {
            // Blink off
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;
            g_state = S_NAME_OFF;
            g_state_start_tick = now;
        }
        break;

    case S_NAME_OFF:
        if (elapsed >= 500)
        {
            // Show version: V1.1
            g_disp_buf[0] = SEG_V;
            g_disp_buf[1] = seg7[1] | 0x80;   // "1." with decimal point
            g_disp_buf[2] = seg7[1];          // "1"
            g_disp_buf[3] = 0x00;
            g_disp_buf[4] = 0x00;
            g_disp_buf[5] = 0x00;
            g_disp_buf[6] = 0x00;
            g_disp_buf[7] = 0x00;
            g_led_state = 0xFF;               // LEDs off
            g_state = S_VER;
            g_state_start_tick = now;
        }
        break;

    case S_VER:
        if (elapsed >= 2000)   // stay at least 1 second 
        {
            // Initialize clock and enter clock display
            hours = 0;
            minutes = 0;
            seconds = 0;
            UpdateClockDisplay();
            g_state = S_CLOCK;
            g_state_start_tick = now;
            g_led_hb = 1;
            g_hb_timer = now;
            g_evt_timer = now;
            LedUpdate();
        }
        break;

    case S_CLOCK:
        // Overrides g_scroll_dir when set: LEFT=0, RIGHT=1.

        // ---- FUNC priority: stop alarm before anything else ----
        if (g_sw1_edge && alarm_ringing)
        {
            g_sw1_edge = 0;
            alarm_ringing = 0;
            alarm_enabled = 0;
            alarm_hitemp = 0;
            alarm_extra_phase = 0;
            alarm_extra_beeps = 0;
            GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
            UpdateClockDisplay();
            SendEvtKey("FUNC");
            UartSendStr("*EVT:ALARM_OFF\r\n");
        }

        // ---- FUNC (SW1): enter / cycle / long-press save & exit ----
        {
            if (g_sw1_edge && g_scroll_active)
            {
                g_sw1_edge = 0;
                g_scroll_active = 0; g_scroll_auto_exit = 0;
                UpdateClockDisplay();
            }
            else if (g_sw1_edge)
            {
                g_sw1_edge = 0;
                g_func_press_tick = now;
                g_func_hold_triggered = 0;
                g_func_entered = 0;
                SendEvtKey("FUNC");

                /* Only enter from NONE on rising edge; cycling waits for release */
                if (g_edit_mode == EDIT_NONE)
                {
                    sv_year = year;  sv_month = month;  sv_day = day;
                    sv_hours = hours;  sv_minutes = minutes;  sv_seconds = seconds;
                    sv_alarm_h = alarm_hours;  sv_alarm_m = alarm_minutes;  sv_alarm_s = alarm_seconds;
                    sv_disp_mode = g_disp_mode;

                    g_edit_mode = EDIT_DATE;
                    g_edit_field = 0;
                    g_disp_mode = DISP_DATE_YYMMDD;
                    g_blink_show = 1;
                    g_blink_timer = now;
                    g_edit_timeout = now + 5000;
                    g_func_entered = 1;     /* entered on this press — suppress cycle on release */
                    UpdateClockDisplay();
                }
                /* else: already editing — just record time, do NOT cycle yet */
            }

            /* Long press (> 1 s) → save & exit (no prior cycle) */
            if (g_edit_mode != EDIT_NONE
                && g_sw1_pressed && !g_func_hold_triggered
                && now - g_func_press_tick >= 1000)
            {
                g_func_hold_triggered = 1;
                if (g_edit_mode == EDIT_ALARM) alarm_enabled = 1;
                g_disp_mode = sv_disp_mode;
                g_edit_timeout = 0;
                g_ntp_state = 0;
                UpdateClockDisplay();
                SendEvtEditSave();
            }

            /* Short press (release before 1 s while in edit mode) → cycle */
            if (!g_sw1_pressed && g_func_press_tick
                && g_edit_mode != EDIT_NONE && !g_func_hold_triggered
                && !g_func_entered)
            {
                g_func_press_tick = 0;
                g_func_entered = 0;
                if (g_edit_mode >= EDIT_ALARM)
                {
                    /* Exit: restore originals */
                    year = sv_year;  month = sv_month;  day = sv_day;
                    hours = sv_hours;  minutes = sv_minutes;  seconds = sv_seconds;
                    alarm_hours = sv_alarm_h;  alarm_minutes = sv_alarm_m;  alarm_seconds = sv_alarm_s;
                    g_disp_mode = sv_disp_mode;
                    g_edit_mode = EDIT_NONE;
                    g_edit_timeout = 0;
                }
                else
                {
                    g_edit_mode++;
                    g_edit_field = 0;
                    if (g_edit_mode == EDIT_TIME)
                        g_disp_mode = DISP_TIME;
                    g_edit_timeout = now + 5000;
                    g_blink_timer = now;
                }
                UpdateClockDisplay();
            }
        }

        // ---- SHIFT (SW2): cycle edit field ----
        if (g_sw2_edge && g_scroll_active)
        {
            g_sw2_edge = 0;
            g_scroll_active = 0; g_scroll_auto_exit = 0;
            UpdateClockDisplay();
        }
        else if (g_sw2_edge && g_edit_mode != EDIT_NONE)
        {
            g_sw2_edge = 0;
            SendEvtKey("SHIFT");
            g_edit_field++;
            if ((g_edit_mode == EDIT_ALARM && g_edit_field > 2)
                || g_edit_field > 2)
                g_edit_field = 0;
            g_edit_timeout = now + 5000;
            UpdateClockDisplay();
        }

        // ---- ADD (SW3): single increment on edge + auto-repeat when held ----
        if (g_sw3_edge && g_scroll_active)
        {
            g_sw3_edge = 0;
            g_scroll_active = 0; g_scroll_auto_exit = 0;
            UpdateClockDisplay();
        }
        else if (g_edit_mode != EDIT_NONE)
        {
            if (g_sw3_edge)
            {
                g_sw3_edge = 0;
                SendEvtKey("ADD");
                g_add_repeat_next = now + 200;  /* first repeat after 200ms hold */
                EditIncrement();
                g_edit_timeout = now + 5000;
                UpdateClockDisplay();
            }
            else if (g_sw3_pressed && now >= g_add_repeat_next)
            {
                g_add_repeat_next = now + 200;  /* 5 Hz = 200ms interval */
                EditIncrement();
                g_edit_timeout = now + 5000;
                UpdateClockDisplay();
            }
        }

        // ---- SAVE (SW4): in edit mode → save & exit ----
        if (g_sw4_edge && g_scroll_active)
        {
            g_sw4_edge = 0;
            g_scroll_active = 0; g_scroll_auto_exit = 0;
            UpdateClockDisplay();
        }
        else if (g_sw4_edge && g_edit_mode != EDIT_NONE)
        {
            g_sw4_edge = 0;
            SendEvtKey("SAVE");
            if (g_edit_mode == EDIT_ALARM)          /* saving alarm = enable it */
                alarm_enabled = 1;
            g_disp_mode = sv_disp_mode;
            g_edit_timeout = 0;
            g_ntp_state = 0;
            UpdateClockDisplay();
            SendEvtEditSave();
            }
        // ---- 5 s timeout: exit edit, restore originals ----
        if (g_edit_mode != EDIT_NONE && now >= g_edit_timeout)
        {
            year = sv_year;  month = sv_month;  day = sv_day;
            hours = sv_hours;  minutes = sv_minutes;  seconds = sv_seconds;
            alarm_hours = sv_alarm_h;  alarm_minutes = sv_alarm_m;  alarm_seconds = sv_alarm_s;
            g_disp_mode = sv_disp_mode;
            g_edit_mode = EDIT_NONE;
            g_edit_timeout = 0;
            UpdateClockDisplay();
        }

        // ---- Blink toggle (250 ms) during edit ----
        if (g_edit_mode != EDIT_NONE && now - g_blink_timer >= 250)
        {
            g_blink_timer = now;
            g_blink_show = !g_blink_show;
            UpdateClockDisplay();
        }

        // ---- SPEED (SW6): scroll speed toggle / exit scroll ----
        if (g_sw6_edge && g_edit_mode == EDIT_NONE)
        {
            g_sw6_edge = 0;
            if (g_scroll_active) {
                g_scroll_active = 0; g_scroll_auto_exit = 0;
                UpdateClockDisplay();
            } else {
                g_scroll_speed = !g_scroll_speed;
            }
            SendEvtKey("SPEED");
        }

        // ---- FORMAT (SW7): toggle direction / exit scroll ----
        if (g_sw7_edge && g_edit_mode == EDIT_NONE)
        {
            g_sw7_edge = 0;
            if (g_scroll_active) {
                g_scroll_active = 0; g_scroll_auto_exit = 0;
                UpdateClockDisplay();
            } else {
                g_scroll_dir = !g_scroll_dir;
                UpdateClockDisplay();
            }
            SendEvtKey("FORMAT");
        }

        // ---- EXT (SW8): reserved key / exit scroll ----
        if (g_sw8_edge && g_edit_mode == EDIT_NONE)
        {
            g_sw8_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            SendEvtKey("EXT");
        }

        // ---- USER1 (GPIO): request PC time sync / exit scroll ----
        if (g_user1_edge && g_edit_mode == EDIT_NONE)
        {
            g_user1_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            else { g_ntp_state = 1; g_ntp_timer = now; g_ntp_start = now; g_ntp_blink = 1; }
            SendEvtKey("USER1");
        }

        // ---- USER2 (GPIO): show weather / exit scroll ----
        if (g_user2_edge && g_edit_mode == EDIT_NONE)
        {
            g_user2_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            else { g_weather_until = now + 5000; UpdateClockDisplay(); }
            SendEvtKey("USER2");
        }

        // ---- Static message timeout (≤8 chars) ----
        if (g_msg_static_until && now >= g_msg_static_until)
        {
            g_msg_static_until = 0;
            UpdateClockDisplay();
        }


        // ---- Weather display timeout ----
        if (g_weather_until && now >= g_weather_until)
        {
            g_weather_until = 0;
            UpdateClockDisplay();
        }

        // ---- NTP sync blink (500ms = 1Hz) / timeout 10s ----
        if (g_ntp_state == 1)
        {
            if (now - g_ntp_timer >= 500) {
                g_ntp_timer = now;
                g_ntp_blink = !g_ntp_blink;
            }
            /* 10s timeout: PC did not respond, abort sync */
            if (now - g_ntp_start >= 10000) {
                g_ntp_state = 0;
            }
        }
        /* LED7 accuracy: controlled by 2s diff check in LedUpdate */

        // ---- DISP (SW5): cycle format / exit scroll ----
        if (g_sw5_edge && g_edit_mode == EDIT_NONE)
        {
            g_sw5_edge = 0;
            SendEvtKey("DISP");
            if (g_scroll_active)
                g_scroll_active = 0;
            else
            {
                g_disp_mode++;
                if (g_disp_mode > DISP_DATE_YYYYMMDD)
                    g_disp_mode = DISP_TIME;
            }
            UpdateClockDisplay();
        }

        // ---- Time update (always runs, even during edit) ----
        if (elapsed >= 1000)
        {
            g_state_start_tick += 1000;
            seconds++;
            if (seconds >= 60)
            {
                seconds = 0;
                minutes++;
                if (minutes >= 60)
                {
                    minutes = 0;
                    hours++;
                    if (hours >= 24)
                    {
                        hours = 0;
                        IncrementDate();
                    }
                }
            }
            if (!g_scroll_active && g_edit_mode == EDIT_NONE)
                UpdateClockDisplay();

            /* Check alarm match (suppressed in NIGHT mode) */
            if (alarm_enabled && !alarm_ringing && !g_mode_night
                && hours == alarm_hours
                && minutes == alarm_minutes
                && seconds == alarm_seconds)
            {
                alarm_ringing = 1;
                alarm_ring_start = now;
                alarm_buzzer_timer = now;
                alarm_led_timer = now;
                alarm_buzzer_on = 1;
                alarm_extra_beeps = 0;
                alarm_hitemp = 0;
                alarm_extra_phase = 0;
                if (g_weather_valid && (g_weather_cond == 4 || g_weather_cond == 5))
                    alarm_extra_beeps = 6;  /* RAI/SNO: 3 extra beeps (6 half-cycles) */
                if (g_weather_valid && g_weather_temp >= 30)
                    alarm_hitemp = 1;       /* ≥30°C: slow flash all LEDs */
                GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, BUZZER_PIN);
                g_led_state = 0xFF;  /* all off, alarm blink takes over */
                UartSendStr("*EVT:ALARM\r\n");
            }
        }

        // ---- Alarm ringing update ----
        if (alarm_ringing)
        {
            /* Enter extra-beep phase when normal 10s ends and extra beeps remain */
            if (now - alarm_ring_start >= 10000 && !alarm_extra_phase && alarm_extra_beeps > 0)
            {
                alarm_extra_phase = 1;
                alarm_extra_timer = now;
                alarm_buzzer_on = 0;
                GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
            }

            /* Auto-stop after 10s (no extra beeps) */
            if (now - alarm_ring_start >= 10000 && !alarm_extra_phase)
            {
                alarm_ringing = 0;
                alarm_enabled = 0;
                alarm_hitemp = 0;
                GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
                UartSendStr("*EVT:ALARM_OFF\r\n");
            }

            /* Normal buzzer: 250ms on/off during first 10s */
            if (alarm_ringing && !alarm_extra_phase)
            {
                if (now - alarm_buzzer_timer >= 250)
                {
                    alarm_buzzer_timer = now;
                    alarm_buzzer_on = !alarm_buzzer_on;
                    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN,
                                 alarm_buzzer_on ? BUZZER_PIN : 0);
                }
            }

            /* Extra beep phase: RAI/SNO — 3 beeps after normal alarm ends */
            if (alarm_extra_phase && alarm_ringing)
            {
                if (now - alarm_extra_timer >= 250)
                {
                    alarm_extra_timer = now;
                    alarm_buzzer_on = !alarm_buzzer_on;
                    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN,
                                 alarm_buzzer_on ? BUZZER_PIN : 0);
                    if (alarm_buzzer_on)
                        alarm_extra_beeps--;
                    if (alarm_extra_beeps == 0) {
                        alarm_ringing = 0;
                        alarm_enabled = 0;
                        alarm_hitemp = 0;
                        alarm_extra_phase = 0;
                        GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
                        UartSendStr("*EVT:ALARM_OFF\r\n");
                    }
                }
            }

            /* LED: high-temp slow flash (500ms all LEDs) else LED1 fast blink (100ms) */
            if (alarm_hitemp)
            {
                if (now - alarm_led_timer >= 500)
                {
                    alarm_led_timer = now;
                    g_led_state ^= 0xFF;
                }
            }
            else
            {
                if (now - alarm_led_timer >= 100)
                {
                    alarm_led_timer = now;
                    g_led_state ^= (1u << LED_ALM_POS);
                }
            }
        }

        // ---- *SET:BEEP timer ----
        if (g_beep_until && now >= g_beep_until)
        {
            g_beep_until = 0;
            GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
        }

        // ---- Scroll advance (paused in edit mode) ----
        if (g_scroll_active && g_edit_mode == EDIT_NONE)
        {
            uint32_t interval = (g_scroll_speed == 0) ? 300 : 100;
            if (now - g_scroll_timer >= interval)
            {
                ScrollAdvance();
                g_scroll_timer = now;
                ScrollRender();
                if (g_scroll_auto_exit && g_scroll_cycles >= ((uint16_t)g_scroll_len + 2)) {
                    g_scroll_active = 0;
                    g_scroll_auto_exit = 0;
                    UpdateClockDisplay();
                }
            }
        }

        // ---- Rain/snow LED blink (1Hz = 500ms) ----
        if (now - g_wx_rain_timer >= 500)
        {
            g_wx_rain_timer = now;
            g_wx_rain_blink = !g_wx_rain_blink;
        }

        // ---- Heartbeat toggle (~500ms) ----
        if (now - g_hb_timer >= 500)
        {
            g_hb_timer = now;
            g_led_hb = !g_led_hb;
        }

        // ---- 1-second EVT heartbeat ----
        if (now - g_evt_timer >= 1000 && !g_evt_suppress)
        {
            g_evt_timer = now;
            SendEvtDisp();
            SendEvtLed();
        }

        // ---- Compose LED indicators (skip during alarm to preserve flash) ----
        if (!alarm_ringing)
            LedUpdate();

        break;

    default:
        break;
    }
}

//---------------------------------------------------------------------------
// Update display buffer.  If scroll debug mode is active, delegate to
// ScrollRender(); otherwise render time/date in the selected format.
//   DISP_TIME:          HH.MM.SS
//   DISP_DATE_YYMMDD:   YY.MM.DD
//   DISP_DATE_YYYYMMDD: YYYY.MMDD
//---------------------------------------------------------------------------
void UpdateClockDisplay(void)
{
    uint8_t i;

    if (g_disp_off) { memset(g_disp_buf, 0x00, 8); return; }

    /* ≤8 char static message override */
    if (g_msg_static_until && g_edit_mode == EDIT_NONE)
        return;

    /* Weather display override (USER2 pressed, 5s timer active) */
    if (g_weather_until && g_edit_mode == EDIT_NONE && !g_scroll_active) {
        if (g_weather_valid) {
            uint8_t wi;
            for (wi = 0; wi < 8; wi++)
                g_disp_buf[wi] = g_weather_buf[wi];
        } else {
            /* No weather data: display --°C--- */
            g_disp_buf[0] = 0x40;              /* '-' */
            g_disp_buf[1] = 0x40;              /* '-' */
            g_disp_buf[2] = 0x63;              /* '°' (a,b,g,f) */
            g_disp_buf[3] = 0x39;              /* 'C' */
            g_disp_buf[4] = 0x40;              /* '-' */
            g_disp_buf[5] = 0x40;              /* '-' */
            g_disp_buf[6] = 0x40;              /* '-' */
            g_disp_buf[7] = 0x00;              /* ' ' */
        }
        return;
    }

    if (g_scroll_active && g_edit_mode == EDIT_NONE)
    {
        ScrollRender();
        return;
    }

    // --- Render base content ---
    if (g_edit_mode == EDIT_ALARM)
    {
        g_disp_buf[0] = SEG_A;
        g_disp_buf[1] = SEG_L;
        g_disp_buf[2] = seg7[alarm_hours / 10];
        g_disp_buf[3] = seg7[alarm_hours % 10];
        g_disp_buf[4] = seg7[alarm_minutes / 10];
        g_disp_buf[5] = seg7[alarm_minutes % 10];
        g_disp_buf[6] = seg7[alarm_seconds / 10];
        g_disp_buf[7] = seg7[alarm_seconds % 10];
    }
    else
    {
        switch (g_disp_mode)
        {
        case DISP_TIME:
            g_disp_buf[0] = seg7[hours / 10];
            g_disp_buf[1] = seg7[hours % 10] | 0x80;
            g_disp_buf[2] = seg7[minutes / 10];
            g_disp_buf[3] = seg7[minutes % 10] | 0x80;
            g_disp_buf[4] = seg7[seconds / 10];
            g_disp_buf[5] = seg7[seconds % 10];
            g_disp_buf[6] = 0x00;
            g_disp_buf[7] = 0x00;
            break;

        case DISP_DATE_YYMMDD:
            g_disp_buf[0] = seg7[(year % 100) / 10];
            g_disp_buf[1] = seg7[(year % 100) % 10] | 0x80;
            g_disp_buf[2] = seg7[month / 10];
            g_disp_buf[3] = seg7[month % 10] | 0x80;
            g_disp_buf[4] = seg7[day / 10];
            g_disp_buf[5] = seg7[day % 10];
            g_disp_buf[6] = 0x00;
            g_disp_buf[7] = 0x00;
            break;

        case DISP_DATE_YYYYMMDD:
            g_disp_buf[0] = seg7[(year / 1000) % 10];
            g_disp_buf[1] = seg7[(year / 100) % 10];
            g_disp_buf[2] = seg7[(year / 10) % 10];
            g_disp_buf[3] = seg7[year % 10] | 0x80;
            g_disp_buf[4] = seg7[month / 10];
            g_disp_buf[5] = seg7[month % 10];
            g_disp_buf[6] = seg7[day / 10];
            g_disp_buf[7] = seg7[day % 10];
            break;
        }
    }

    /* NIGHT mode: only show hours:minutes (4 digits), clear seconds/date */
    if (g_mode_night && g_edit_mode == EDIT_NONE)
    {
        g_disp_buf[4] = 0x00;
        g_disp_buf[5] = 0x00;
        g_disp_buf[6] = 0x00;
        g_disp_buf[7] = 0x00;
    }

    // --- Blink overlay: blank the selected field digits ---
    if (g_edit_mode != EDIT_NONE && !g_blink_show)
    {
        uint8_t ds, de;

        switch (g_edit_mode)
        {
        case EDIT_DATE:
            if (g_disp_mode == DISP_DATE_YYYYMMDD)
            {
                if (g_edit_field == 0)      { ds = 0; de = 3; }
                else if (g_edit_field == 1) { ds = 4; de = 5; }
                else                        { ds = 6; de = 7; }
            }
            else
            {
                if (g_edit_field == 0)      { ds = 0; de = 1; }
                else if (g_edit_field == 1) { ds = 2; de = 3; }
                else                        { ds = 4; de = 5; }
            }
            break;

        case EDIT_TIME:
            if (g_edit_field == 0)      { ds = 0; de = 1; }
            else if (g_edit_field == 1) { ds = 2; de = 3; }
            else                        { ds = 4; de = 5; }
            break;

        case EDIT_ALARM:
            if (g_edit_field == 0)      { ds = 2; de = 3; }
            else if (g_edit_field == 1) { ds = 4; de = 5; }
            else                        { ds = 6; de = 7; }
            break;

        default:
            ds = 0; de = 7;  break;
        }

        for (i = ds; i <= de; i++)
            g_disp_buf[i] = 0x00;
    }

    /* FORMAT RIGHT: reverse display in-place (non-edit, non-scroll) */
    if (g_scroll_dir && !g_scroll_active && g_edit_mode == EDIT_NONE) {
        uint8_t j, rev[8];
        uint8_t dp = 0;
        /* collect dp bits before stripping */
        for (j = 0; j < 8; j++)
            if (g_disp_buf[j] & 0x80) dp |= (1u << j);
        /* reverse segment values (strip dp) */
        for (j = 0; j < 8; j++)
            rev[j] = g_disp_buf[7 - j] & 0x7F;
        /* reposition dp: shift each dp bit left by one position */
        for (j = 0; j < 7; j++)
            if (dp & (1u << j)) rev[6 - j] |= 0x80;
        for (j = 0; j < 8; j++)
            g_disp_buf[j] = rev[j];
    }
}

//---------------------------------------------------------------------------
// Helper: debounce one button from raw TCA6424 Port0 bit
//---------------------------------------------------------------------------
static void DebounceOne(uint8_t raw_bit, uint8_t *p_pressed, uint8_t *p_edge,
                        uint8_t *p_count, uint8_t *p_last)
{
    if (raw_bit == *p_last)
    {
        if (*p_count < SW_DEBOUNCE_MS)
            (*p_count)++;
    }
    else
    {
        *p_count = 0;
        *p_last = raw_bit;
    }

    if (*p_count == SW_DEBOUNCE_MS)
    {
        uint8_t debounced = !raw_bit;          // active-low → active-high
        if (debounced && !*p_pressed)
            *p_edge = 1;                       // rising edge detected
        *p_pressed = debounced;
    }
}

//---------------------------------------------------------------------------
// Read all buttons from TCA6424 Port0 (K1-K8) and GPIO (USER1/USER2)
// Must be called every 1ms for correct debounce timing.
//---------------------------------------------------------------------------
void ReadButtons(void)
{
    static uint8_t c1 = 0, l1 = 1;
    static uint8_t c2 = 0, l2 = 1;
    static uint8_t c3 = 0, l3 = 1;
    static uint8_t c4 = 0, l4 = 1;
    static uint8_t c5 = 0, l5 = 1;
    static uint8_t c6 = 0, l6 = 1;
    static uint8_t c7 = 0, l7 = 1;
    static uint8_t c8 = 0, l8 = 1;
    static uint8_t cu1 = 0, lu1 = 1;
    static uint8_t cu2 = 0, lu2 = 1;
    uint8_t raw;

    raw = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);

    DebounceOne((raw & SW1_PIN) ? 1 : 0, &g_sw1_pressed, &g_sw1_edge, &c1, &l1);
    DebounceOne((raw & SW2_PIN) ? 1 : 0, &g_sw2_pressed, &g_sw2_edge, &c2, &l2);
    DebounceOne((raw & SW3_PIN) ? 1 : 0, &g_sw3_pressed, &g_sw3_edge, &c3, &l3);
    DebounceOne((raw & SW4_PIN) ? 1 : 0, &g_sw4_pressed, &g_sw4_edge, &c4, &l4);
    DebounceOne((raw & SW5_PIN) ? 1 : 0, &g_sw5_pressed, &g_sw5_edge, &c5, &l5);
    DebounceOne((raw & SW6_PIN) ? 1 : 0, &g_sw6_pressed, &g_sw6_edge, &c6, &l6);
    DebounceOne((raw & SW7_PIN) ? 1 : 0, &g_sw7_pressed, &g_sw7_edge, &c7, &l7);
    DebounceOne((raw & SW8_PIN) ? 1 : 0, &g_sw8_pressed, &g_sw8_edge, &c8, &l8);

    /* USER1 (PJ0) and USER2 (PJ1): read directly from GPIO, active low */
    raw = (uint8_t)GPIOPinRead(USER1_PORT, USER1_PIN | USER2_PIN);
    DebounceOne((raw & USER1_PIN) ? 1 : 0, &g_user1_pressed, &g_user1_edge, &cu1, &lu1);
    DebounceOne((raw & USER2_PIN) ? 1 : 0, &g_user2_pressed, &g_user2_edge, &cu2, &lu2);
}

//---------------------------------------------------------------------------
// Return number of days in a given month (leap-year-aware)
//---------------------------------------------------------------------------
uint8_t DaysInMonth(uint16_t y, uint8_t m)
{
    if (m == 2)
    {
        // Leap year: divisible by 400, or divisible by 4 but not by 100
        if ((y % 400 == 0) || ((y % 4 == 0) && (y % 100 != 0)))
            return 29;
        return 28;
    }
    // Apr(4), Jun(6), Sep(9), Nov(11) have 30 days
    if (m == 4 || m == 6 || m == 9 || m == 11)
        return 30;
    return 31;
}

//---------------------------------------------------------------------------
// Increment date by one day with correct month-end and leap-year rollover
//---------------------------------------------------------------------------
void IncrementDate(void)
{
    day++;
    if (day > DaysInMonth(year, month))
    {
        day = 1;
        month++;
        if (month > 12)
        {
            month = 1;
            year++;
        }
    }
}

//---------------------------------------------------------------------------
// ADD (K3) handler: increment current edit field with carry & range clamp
//---------------------------------------------------------------------------
void EditIncrement(void)
{
    switch (g_edit_mode)
    {
    case EDIT_TIME:
        if (g_edit_field == 0)          // hours
            hours = (hours + 1) % 24;
        else if (g_edit_field == 1)     // minutes
            minutes = (minutes + 1) % 60;
        else                            // seconds
            seconds = (seconds + 1) % 60;
        break;

    case EDIT_DATE:
        if (g_edit_field == 0)          // year
        {
            year++;
            if (year > 2099) year = 2000;
        }
        else if (g_edit_field == 1)     // month
        {
            month = (month % 12) + 1;
            if (day > DaysInMonth(year, month))
                day = DaysInMonth(year, month);
        }
        else                            // day
        {
            day++;
            if (day > DaysInMonth(year, month))
                day = 1;
        }
        break;

    case EDIT_ALARM:
        if (g_edit_field == 0)          // alarm hours
            alarm_hours = (alarm_hours + 1) % 24;
        else if (g_edit_field == 1)     // alarm minutes
            alarm_minutes = (alarm_minutes + 1) % 60;
        else                            // alarm seconds
            alarm_seconds = (alarm_seconds + 1) % 60;
        break;

    default:
        break;
    }
}

//---------------------------------------------------------------------------
// Initialise scroll mode with data to display.
// seg[] — segment patterns (must remain valid while scrolling)
// dp[]  — decimal-point flags (may be NULL if none)
// len   — number of items in seg[]/dp[]
//---------------------------------------------------------------------------
void ScrollInit(const uint8_t *seg, const uint8_t *dp, uint8_t len)
{
    g_pscroll_seg = seg;
    g_pscroll_dp = dp;
    g_scroll_len = len;
    g_scroll_pos = 0;
    g_scroll_active = 1;
}

//---------------------------------------------------------------------------
// Render current scroll frame into g_disp_buf.
// Data forms a continuous ring: data[0..len-1] + 2-blank-gap + data[0..] ...
//
// LEFT→RIGHT (g_scroll_dir=0, pos↑):
//   Data in normal order, scrolls left, DP from current data position.
// RIGHT→LEFT (g_scroll_dir=1, pos↓):
//   Data reversed (data[len-1-v] at virtual v), scrolls right,
//   DP shifted left by 2 from reversed position.
//---------------------------------------------------------------------------
void ScrollRender(void)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        uint16_t idx = (g_scroll_pos + (uint16_t)i) % ((uint16_t)g_scroll_len + 2);
        uint8_t seg = 0x00;

        if (idx < g_scroll_len)
        {
            if (g_scroll_dir == 0)
            {
                // LEFT→RIGHT: same order, DP on current digit
                seg = g_pscroll_seg[idx];
                if (g_pscroll_dp && g_pscroll_dp[idx])
                    seg |= 0x80;
            }
            else
            {
                // RIGHT→LEFT: reversed data, DP shifted left by 1
                uint16_t rev = (uint16_t)g_scroll_len - 1 - idx;
                seg = g_pscroll_seg[rev];
                if (g_pscroll_dp && rev >= 1)
                {
                    uint16_t dp_idx = rev - 1;
                    if (g_pscroll_dp[dp_idx])
                        seg |= 0x80;
                }
            }
        }
        g_disp_buf[i] = seg;
    }
}

//---------------------------------------------------------------------------
// Advance scroll position (continuous wrap with 2-blank gap)
//   LEFT→RIGHT: position increases (content scrolls left)
//   RIGHT→LEFT: position decreases (content scrolls right)
//---------------------------------------------------------------------------
void ScrollAdvance(void)
{
    uint16_t virt_len = (uint16_t)g_scroll_len + 2;

    if (g_scroll_dir == 0)
    {
        g_scroll_pos++;
        if (g_scroll_pos >= virt_len)
            g_scroll_pos = 0;
    }
    else
    {
        if (g_scroll_pos == 0)
            g_scroll_pos = virt_len - 1;
        else
            g_scroll_pos--;
    }
    if (g_scroll_auto_exit)
        g_scroll_cycles++;
}

//---------------------------------------------------------------------------
static uint32_t time_to_sec(uint8_t h, uint8_t m, uint8_t s)
{
    return (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
}

// Compose g_led_state from indicator flags (active-low PCA9557)
// Derives ALM from alarm_enabled and EDT from g_edit_mode automatically.
// Not called during alarm ringing to preserve the all-LED flash pattern.
//---------------------------------------------------------------------------
static void LedUpdate(void)
{
    uint32_t now = g_ui32SysTickCount;
    uint8_t val;

    if (g_led_override) return;   /* PC has direct control */

    val = 0xFF;

    /* NIGHT mode: only heartbeat LED, suppress all others */
    if (g_mode_night) {
        if (g_led_hb) val &= ~(1u << LED_HB_POS);
        g_led_state = val;
        return;
    }

    if (g_led_hb)                     val &= ~(1u << LED_HB_POS);       /* LED0 HB */
    if (alarm_enabled && !alarm_ringing) val &= ~(1u << LED_ALM_POS);  /* LED1 ALM steady when armed */
    if (g_edit_mode != EDIT_NONE)     val &= ~(1u << LED_EDT_POS);     /* LED2 EDT */
    if (now - g_tx_led_timer < 100)   val &= ~(1u << LED_UART_POS);    /* LED3 UART TX */
    if (now - g_rx_led_timer < 100)   val &= ~(1u << LED_UART_POS);    /* LED3 UART RX */
    if (g_weather_valid && g_weather_cond == 1) val &= ~(1u << LED_WX_SUN_POS);  /* LED4 SUN */
    if (g_weather_valid && (g_weather_cond == 4 || g_weather_cond == 5) && g_wx_rain_blink)
                                          val &= ~(1u << LED_WX_RAIN_POS); /* LED5 RAI/SNO 1Hz */
    if (g_weather_valid && g_weather_temp >= 30) val &= ~(1u << LED_TEMP_POS); /* LED6 ≥30°C */
    if (g_ntp_state == 1 && g_ntp_blink) val &= ~(1u << LED_NTP_POS);   /* LED7 NTP syncing */
    if (g_last_ntp_tick != 0) {
        uint32_t elapsed_ms = now - g_last_ntp_tick;
        uint32_t elapsed_s = elapsed_ms / 1000;
        uint32_t ntp_sec = time_to_sec(g_last_ntp_h, g_last_ntp_m, g_last_ntp_s) + elapsed_s;
        uint32_t disp_sec = time_to_sec(hours, minutes, seconds);
        uint32_t diff = (ntp_sec > disp_sec) ? (ntp_sec - disp_sec) : (disp_sec - ntp_sec);
        if (diff > 43200) diff = 86400 - diff; /* handle midnight wrap */
        if (diff <= 2) val &= ~(1u << LED_NTP_POS); /* LED7 on if within 2s */
    }

    g_led_state = val;
}

//---------------------------------------------------------------------------
// Basic GPIO initialization (PF0 for debug, PJ0/PJ1 as inputs with pull-up)
//---------------------------------------------------------------------------
void S800_GPIO_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ));
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOK));

    // PK5 as output (buzzer, ULN2003 inverts: high→buzzer on)
    GPIOPinTypeGPIOOutput(GPIO_PORTK_BASE, GPIO_PIN_5);
    GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_5, 0);
    // PJ0 and PJ1 as inputs with pull-up (button not used, but kept)
    GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
}

//---------------------------------------------------------------------------
// I2C0 initialization and IO expander configuration
//---------------------------------------------------------------------------
void S800_I2C0_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);

    I2CMasterInitExpClk(I2C0_BASE, ui32SysClock, true);
    I2CMasterEnable(I2C0_BASE);

    // TCA6424: Port0 input, Port1 & Port2 outputs
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0xFF);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x00);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x00);

    // PCA9557: all outputs, LEDs initially off (0xFF = off, active low)
    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00);
    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
}

//---------------------------------------------------------------------------
// I2C byte write with register address
//---------------------------------------------------------------------------
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData)
{
    uint8_t rop;
    while (I2CMasterBusy(I2C0_BASE));
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
    I2CMasterDataPut(I2C0_BASE, RegAddr);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(I2C0_BASE));
    rop = (uint8_t)I2CMasterErr(I2C0_BASE);
    I2CMasterDataPut(I2C0_BASE, WriteData);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
    while (I2CMasterBusy(I2C0_BASE));
    rop = (uint8_t)I2CMasterErr(I2C0_BASE);
    return rop;
}

//---------------------------------------------------------------------------
// I2C byte read from register address (used by ReadButtons)
// Uses I2CMasterBusy (module ready) not BusBusy (bus free) for correct timing.
//---------------------------------------------------------------------------
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr)
{
    uint8_t value;

    // Phase 1: send register address
    while (I2CMasterBusy(I2C0_BASE));
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
    I2CMasterDataPut(I2C0_BASE, RegAddr);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND);
    while (I2CMasterBusy(I2C0_BASE));

    // Phase 2: repeated start + read data
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, true);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE);
    while (I2CMasterBusy(I2C0_BASE));
    value = I2CMasterDataGet(I2C0_BASE);

    return value;
}

//---------------------------------------------------------------------------
// Simple delay loop (not used for timing, kept for compatibility)
//---------------------------------------------------------------------------
void Delay(uint32_t value)
{
    uint32_t ui32Loop;
    for (ui32Loop = 0; ui32Loop < value; ui32Loop++);
}

//=============================================================================
// UART0 Initialization (PA0=RX, PA1=TX, 115200 8N1)
//=============================================================================
static void UartInit(void)
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

//=============================================================================
// UART0 Interrupt Handler — fill ring buffer
//=============================================================================
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

//=============================================================================
// UART low-level send helpers
//=============================================================================
static void UartSendChar(char c)
{
    UARTCharPut(UART0_BASE, (uint8_t)c);
    g_tx_led_timer = g_ui32SysTickCount;
}

static void UartSendStr(const char *s)
{
    while (*s) UartSendChar(*s++);
}

static void UartSendDec(uint32_t v)
{
    char buf[12];
    uint8_t i = 0;
    if (v == 0) { UartSendChar('0'); return; }
    while (v > 0 && i < sizeof(buf) - 1) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) UartSendChar(buf[--i]);
}

static void UartSendHexByte(uint8_t v)
{
    const char hex[] = "0123456789ABCDEF";
    UartSendChar(hex[v >> 4]);
    UartSendChar(hex[v & 0x0F]);
}

//=============================================================================
// Skip whitespace, return pointer to first non-space
//=============================================================================
static const char *skip_spaces(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

//=============================================================================
// Abbreviation-aware keyword match.
// pattern uses uppercase=required, lowercase=optional per spec.
// Returns 1 if str matches at the current position, 0 otherwise.
//=============================================================================
static int match_kw(const char *str, const char *pattern)
{
    const char *s = str;
    const char *p = pattern;
    while (*p && *s) {
        char pc = *p;
        char sc = *s;
        if (*s == ' ' || *s == '\t' || *s == ':' || *s == '\0')
            break;
        if (pc >= 'a' && pc <= 'z') pc -= 32;
        if (sc >= 'a' && sc <= 'z') sc -= 32;
        if (pc != sc) return 0;
        s++; p++;
    }
    while (*p) {
        if (*p < 'a' || *p > 'z') return 0;
        p++;
    }
    return (*s == '\0' || *s == ' ' || *s == '\t' || *s == ':');
}

//=============================================================================
// Skip past a keyword token (alphanumeric sequence) from current position.
// Returns pointer to first character after the keyword.
//=============================================================================
static const char *skip_kw(const char *p)
{
    while (*p && *p != ' ' && *p != '\t' && *p != '\0')
        p++;
    return p;
}

//=============================================================================
// Parse decimal number from string, advance pointer
//=============================================================================
static int parse_dec(const char **pp, uint32_t *val)
{
    const char *p = *pp;
    uint32_t v = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    if (neg) v = (uint32_t)(-(int32_t)v);
    *val = v;
    *pp = p;
    return 0;
}

//=============================================================================
// Hex nibble to value
//=============================================================================
static uint8_t hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

//=============================================================================
// 7-segment font: convert ASCII printable char to segment pattern
//=============================================================================
static uint8_t CharToSeg7(char c)
{
    if (c >= '0' && c <= '9') return seg7[c - '0'];
    switch (c) {
        case 'A': case 'a': return 0x77; case 'B': case 'b': return 0x7C;
        case 'C': case 'c': return 0x39; case 'D': case 'd': return 0x5E;
        case 'E': case 'e': return 0x79; case 'F': case 'f': return 0x71;
        case 'G': case 'g': return 0x3D; case 'H': case 'h': return 0x76;
        case 'I': case 'i': return 0x30; case 'J': case 'j': return 0x1E;
        case 'K': case 'k': return 0x7A; case 'L': case 'l': return 0x3C;
        case 'M': case 'm': return 0x55; case 'N': case 'n': return 0x37;
        case 'O': case 'o': return 0x3F; case 'P': case 'p': return 0x73;
        case 'Q': case 'q': return 0x67; case 'R': case 'r': return 0x70;
        case 'S': case 's': return 0x6D; case 'T': case 't': return 0x78;
        case 'U': case 'u': return 0x3E; case 'V': case 'v': return 0x7E;
        case 'W': case 'w': return 0x6A; case 'X': case 'x': return 0x36;
        case 'Y': case 'y': return 0x6E; case 'Z': case 'z': return 0x49;
        case '-': return 0x40; case '_': return 0x08; case (char)0xB0: return 0x63; /* '°' */
        case '=': return 0x48; case ' ': return 0x00;
        default:  return 0x00;
    }
}

// Message scroll buffer (persistent storage for *SET:MSG)
static uint8_t g_msg_segs[32];

//=============================================================================
// Reverse-map segment pattern (lower 7 bits) to a display character.
// Returns '?' for unknown patterns.
//=============================================================================
static char SegToChar(uint8_t seg)
{
    uint8_t s7 = seg & 0x7F;   /* mask dp */
    uint8_t i;
    if (s7 == 0x00) return '_';
    for (i = 0; i < 10; i++) { if (s7 == seg7[i]) return (char)('0' + i); }
    if (s7 == SEG_A) return 'A';
    if (s7 == 0x7C) return 'B';
    if (s7 == 0x39) return 'C';
    if (s7 == 0x5E) return 'D';
    if (s7 == 0x79) return 'E';
    if (s7 == 0x3D) return 'G';
    if (s7 == 0x76) return 'H';
    if (s7 == 0x30) return 'I';
    if (s7 == 0x1E) return 'J';
    if (s7 == 0x7A) return 'K';
    if (s7 == SEG_L) return 'L';
    if (s7 == SEG_M) return 'M';
    if (s7 == SEG_N) return 'N';
    if (s7 == 0x3F) return 'O';
    if (s7 == 0x73) return 'P';
    if (s7 == 0x67) return 'Q';
    if (s7 == 0x70) return 'R';
    if (s7 == 0x6D) return 'S';
    if (s7 == 0x78) return 'T';
    if (s7 == SEG_U) return 'U';
    if (s7 == SEG_V) return 'V';
    if (s7 == 0x6A) return 'W';
    if (s7 == 0x36) return 'X';
    if (s7 == SEG_Y) return 'Y';
    if (s7 == 0x49) return 'Z';
    if (s7 == 0x40) return '-';
    if (s7 == 0x63) return (char)0xB0;  /* '°' degree symbol */
    if (s7 == 0x08) return '=';
    return '?';
}

//=============================================================================
// Send *EVT:DISP <8chars> <dpHex>\r\n
//=============================================================================
static void SendEvtDisp(void)
{
    uint8_t i, dp = 0;
    UartSendStr("*EVT:DISP ");
    for (i = 0; i < 8; i++) {
        UartSendChar(SegToChar(g_disp_buf[i]));
        if (g_disp_buf[i] & 0x80) dp |= (1u << i);
    }
    UartSendChar(' ');
    UartSendHexByte(dp);
    UartSendStr("\r\n");
}

//=============================================================================
// Send *EVT:LED <hex2>\r\n
//=============================================================================
static void SendEvtLed(void)
{
    UartSendStr("*EVT:LED ");
    UartSendHexByte(g_led_state);
    UartSendStr("\r\n");
}

//=============================================================================
// Send *EVT:KEY event for a given key name
//=============================================================================
static void SendEvtKey(const char *name)
{
    UartSendStr("*EVT:KEY ");
    UartSendStr(name);
    UartSendStr("\r\n");
}

static void SendEvtEditSave(void)
{
    UartSendStr("*EVT:EDIT ");
    if (g_edit_mode == EDIT_DATE) {
        UartSendStr("DATE ");
        UartSendDec(year); UartSendChar('-');
        UartSendDec(month); UartSendChar('-');
        UartSendDec(day);
    } else if (g_edit_mode == EDIT_TIME) {
        UartSendStr("TIME ");
        UartSendDec(hours); UartSendChar(':');
        UartSendDec(minutes); UartSendChar(':');
        UartSendDec(seconds);
    } else {
        UartSendStr("ALARM ");
        UartSendDec(alarm_hours); UartSendChar(':');
        UartSendDec(alarm_minutes); UartSendChar(':');
        UartSendDec(alarm_seconds);
    }
    UartSendStr("\r\n");
    g_edit_mode = EDIT_NONE;
}

//=============================================================================
// Simple case-insensitive compare (both strings null-terminated)
//=============================================================================
static int scmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == *b);
}

//=============================================================================
// Check if str starts with prefix (case-insensitive)
//=============================================================================
static int spfx(const char *str, const char *pfx)
{
    while (*pfx) {
        char cs = *str, cp = *pfx;
        if (cs >= 'a' && cs <= 'z') cs -= 32;
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        if (cs != cp) return 0;
        str++; pfx++;
    }
    return 1;
}

//=============================================================================
// Command handlers
//=============================================================================

static void CmdSetTime(const char *args)
{
    uint32_t v;
    const char *next, *p;
    int ok = 0;

    p = skip_spaces(args);
    /* Detect positional: "HOUR MIN SEC 12 30 45" */
    next = p;
    if (match_kw(next, "HOUR"))       next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "MINute")) next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "SECond")) next = skip_spaces(skip_kw(next));

    if (next != p && (match_kw(next, "HOUR") || match_kw(next, "MINute") || match_kw(next, "SECond")))
    {
        int has_h = 0, has_m = 0, has_s = 0;
        p = skip_spaces(args);
        while (*p) {
            if (match_kw(p, "HOUR"))        { has_h = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "MINute")) { has_m = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "SECond")) { has_s = 1; p = skip_spaces(skip_kw(p)); }
            else break;
        }
        p = skip_spaces(p);
        if (has_h && parse_dec(&p, &v) == 0) { hours   = (uint8_t)(v % 24); ok = 1; p = skip_spaces(p); }
        if (has_m && parse_dec(&p, &v) == 0) { minutes = (uint8_t)(v % 60); ok = 1; p = skip_spaces(p); }
        if (has_s && parse_dec(&p, &v) == 0) { seconds = (uint8_t)(v % 60); ok = 1; }
    }
    else
    {
        /* Key-value: "HOUR 12 MIN 30 SEC 45" */
        while (*p) {
            if (match_kw(p, "HOUR")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { hours = (uint8_t)(v % 24); ok = 1; }
            } else if (match_kw(p, "MINute")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { minutes = (uint8_t)(v % 60); ok = 1; }
            } else if (match_kw(p, "SECond")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { seconds = (uint8_t)(v % 60); ok = 1; }
            } else break;
            p = skip_spaces(p);
        }
    }
    if (ok) { UartSendStr("OK\r\n"); UpdateClockDisplay();
              if (g_ntp_state == 1) { g_ntp_state = 0;
                g_last_ntp_tick = g_ui32SysTickCount;
                g_last_ntp_h = hours; g_last_ntp_m = minutes; g_last_ntp_s = seconds; } }
    else      UartSendStr("ERROR bad args\r\n");
}

static void CmdSetDate(const char *args)
{
    uint32_t v;
    const char *next, *p;
    int ok = 0;

    p = skip_spaces(args);

    /* Detect positional: "YEAR MONTH DATE 2025 06 01" */
    next = p;
    if (match_kw(next, "YEAR"))       next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "MONTH")) next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "DATE"))  next = skip_spaces(skip_kw(next));

    if (next != p && (match_kw(next, "YEAR") || match_kw(next, "MONTH") || match_kw(next, "DATE")))
    {
        int has_y = 0, has_m = 0, has_d = 0;
        p = skip_spaces(args);
        while (*p) {
            if (match_kw(p, "YEAR"))       { has_y = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "MONTH")) { has_m = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "DATE"))  { has_d = 1; p = skip_spaces(skip_kw(p)); }
            else break;
        }
        p = skip_spaces(p);
        if (has_y && parse_dec(&p, &v) == 0) { if (v >= 2000 && v <= 2099) { year = (uint16_t)v; ok = 1; } p = skip_spaces(p); }
        if (has_m && parse_dec(&p, &v) == 0) { if (v >= 1 && v <= 12) { month = (uint8_t)v; ok = 1; } p = skip_spaces(p); }
        if (has_d && parse_dec(&p, &v) == 0) { if (v >= 1 && v <= 31) { day = (uint8_t)v; ok = 1; } }
    }
    else
    {
        while (*p) {
            if (match_kw(p, "YEAR")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0 && v >= 2000 && v <= 2099) { year = (uint16_t)v; ok = 1; }
            } else if (match_kw(p, "MONTH")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0 && v >= 1 && v <= 12) { month = (uint8_t)v; ok = 1; }
            } else if (match_kw(p, "DATE")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0 && v >= 1 && v <= 31) { day = (uint8_t)v; ok = 1; }
            } else break;
            p = skip_spaces(p);
        }
    }

    if (ok && day > DaysInMonth(year, month)) day = DaysInMonth(year, month);
    if (ok) { UartSendStr("OK\r\n"); UpdateClockDisplay();
              if (g_ntp_state == 1) { g_ntp_state = 0;
                g_last_ntp_tick = g_ui32SysTickCount;
                g_last_ntp_h = hours; g_last_ntp_m = minutes; g_last_ntp_s = seconds; } }
    else      UartSendStr("ERROR bad args\r\n");
}

static void CmdSetAlarm(const char *args)
{
    uint32_t v;
    const char *next, *p;
    int ok = 0;

    p = skip_spaces(args);
    if (scmp(p, "OFF")) {
        alarm_enabled = 0;
        UartSendStr("OK\r\n");
        return;
    }

    /* Detect positional: "HOUR MIN SEC 12 30 45" */
    next = p;
    if (match_kw(next, "HOUR"))       next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "MINute")) next = skip_spaces(skip_kw(next));
    else if (match_kw(next, "SECond")) next = skip_spaces(skip_kw(next));

    if (next != p && (match_kw(next, "HOUR") || match_kw(next, "MINute") || match_kw(next, "SECond")))
    {
        int has_h = 0, has_m = 0, has_s = 0;
        p = skip_spaces(args);
        while (*p) {
            if (match_kw(p, "HOUR"))        { has_h = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "MINute")) { has_m = 1; p = skip_spaces(skip_kw(p)); }
            else if (match_kw(p, "SECond")) { has_s = 1; p = skip_spaces(skip_kw(p)); }
            else break;
        }
        p = skip_spaces(p);
        if (has_h && parse_dec(&p, &v) == 0) { alarm_hours   = (uint8_t)(v % 24); ok = 1; p = skip_spaces(p); }
        if (has_m && parse_dec(&p, &v) == 0) { alarm_minutes = (uint8_t)(v % 60); ok = 1; p = skip_spaces(p); }
        if (has_s && parse_dec(&p, &v) == 0) { alarm_seconds = (uint8_t)(v % 60); ok = 1; }
    }
    else
    {
        while (*p) {
            if (match_kw(p, "HOUR")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { alarm_hours = (uint8_t)(v % 24); ok = 1; }
            } else if (match_kw(p, "MINute")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { alarm_minutes = (uint8_t)(v % 60); ok = 1; }
            } else if (match_kw(p, "SECond")) {
                p = skip_spaces(skip_kw(p));
                if (parse_dec(&p, &v) == 0) { alarm_seconds = (uint8_t)(v % 60); ok = 1; }
            } else break;
            p = skip_spaces(p);
        }
    }
    if (ok) { alarm_enabled = 1; UartSendStr("OK\r\n"); UpdateClockDisplay(); }
    else      UartSendStr("ERROR bad args\r\n");
}

static void CmdSetDisplay(const char *args)
{
    args = skip_spaces(args);
    if (scmp(args, "OFF")) g_disp_off = 1;
    else                   g_disp_off = 0;
    UartSendStr("OK\r\n");
    UpdateClockDisplay();
}

static void CmdSetFormat(const char *args)
{
    args = skip_spaces(args);
    if (scmp(args, "RIGHT")) g_scroll_dir = 1;
    else                     g_scroll_dir = 0;
    UartSendStr("OK\r\n");
}

static void CmdSetMsg(const char *args)
{
    uint8_t i, len = 0;
    args = skip_spaces(args);
    while (*args && len < 32) {
        if (*args == '.') {
            if (len > 0) g_msg_segs[len - 1] |= 0x80;
        } else {
            g_msg_segs[len++] = CharToSeg7(*args);
        }
        args++;
    }
    if (len == 0) { UartSendStr("ERROR empty msg\r\n"); return; }

    if (len > 8) {
        /* Extract DP bits from embedded segment data before scrolling */
        static uint8_t msg_dp[32];
        for (i = 0; i < len; i++) {
            msg_dp[i] = (g_msg_segs[i] & 0x80) ? 1 : 0;
            g_msg_segs[i] &= 0x7F;
        }
        ScrollInit(g_msg_segs, msg_dp, len);
        g_scroll_auto_exit = 1;
        g_scroll_cycles = 0;
    } else {
        /* ≤8 chars: static display for 2.5 seconds */
        for (i = 0; i < len; i++)
            g_disp_buf[i] = g_msg_segs[i];
        for (i = len; i < 8; i++)
            g_disp_buf[i] = 0x00;
        /* FMT_RIGHT: reverse display, DP shifted left by 1 */
        if (g_scroll_dir) {
            uint8_t j, rev[8];
            uint8_t dp = 0;
            for (j = 0; j < 8; j++)
                if (g_disp_buf[j] & 0x80) dp |= (1u << j);
            for (j = 0; j < 8; j++)
                rev[j] = g_disp_buf[7 - j] & 0x7F;
            for (j = 0; j < 7; j++)
                if (dp & (1u << j)) rev[6 - j] |= 0x80;
            for (j = 0; j < 8; j++)
                g_disp_buf[j] = rev[j];
        }
        g_scroll_active = 0;
        g_msg_static_until = g_ui32SysTickCount + 2500;
    }
    UartSendStr("OK\r\n");
}

static void CmdSetBeep(const char *args)
{
    uint32_t ms;
    args = skip_spaces(args);
    if (parse_dec(&args, &ms) == 0) {
        if (ms < 10) ms = 10;
        if (ms > 5000) ms = 5000;
        if (!alarm_ringing) {
            g_beep_until = g_ui32SysTickCount + ms;
            GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, BUZZER_PIN);
        }
        UartSendStr("OK\r\n");
    } else {
        UartSendStr("ERROR bad beep ms\r\n");
    }
}

static void CmdSetLed(const char *args)
{
    uint8_t v;
    args = skip_spaces(args);
    v = (hex_val(args[0]) << 4) | hex_val(args[1]);
    if (v == 0x00) {
        g_led_override = 0;
        LedUpdate();  /* resume normal LED control */
    } else {
        g_led_override = 1;
        g_led_state = v;
    }
    UartSendStr("OK\r\n");
}

static void CmdSetKey(const char *args)
{
    args = skip_spaces(args);
    if      (scmp(args, "FUNC"))   g_sw1_edge = 1;
    else if (scmp(args, "SHIFT"))  g_sw2_edge = 1;
    else if (scmp(args, "ADD"))    g_sw3_edge = 1;
    else if (scmp(args, "SAVE"))   g_sw4_edge = 1;
    else if (scmp(args, "DISP"))   g_sw5_edge = 1;
    else if (scmp(args, "SPEED"))  g_sw6_edge = 1;
    else if (scmp(args, "FORMAT")) g_sw7_edge = 1;
    else if (scmp(args, "EXT") || scmp(args, "USER1") || scmp(args, "USER2"))
        ;  /* acknowledged, no edge set to avoid EVT:KEY loopback */
    UartSendStr("OK\r\n");
}

static void CmdSetWeather(const char *args)
{
    uint8_t i, len;
    int32_t t;
    const char *p, *cond;

    args = skip_spaces(args);
    p = args;

    /* Detect format: hex16 (all hex digits) vs "temp condition" */
    if (p[0] && p[1] && (((p[0]>='0'&&p[0]<='9')||(p[0]>='A'&&p[0]<='F')||(p[0]>='a'&&p[0]<='f')) &&
                         ((p[1]>='0'&&p[1]<='9')||(p[1]>='A'&&p[1]<='F')||(p[1]>='a'&&p[1]<='f'))))
    {
        int is_hex = 1;
        for (i = 0; p[i] && i < 16; i++)
            if (!((p[i]>='0'&&p[i]<='9')||(p[i]>='A'&&p[i]<='F')||(p[i]>='a'&&p[i]<='f')))
                { is_hex = 0; break; }
        if (is_hex && i == 16) {
            /* Hex16 format: 16 hex chars = 8 segment bytes */
            for (i = 0; i < 8; i++) {
                g_weather_buf[i] = (hex_val(p[0]) << 4) | hex_val(p[1]);
                p += 2;
            }
            g_weather_valid = 1;
            UartSendStr("OK\r\n");
            return;
        }
    }

    /* "temp condition" format, e.g. "25 SUN" or "-5 SNOW" */
    if (parse_dec(&args, (uint32_t*)&t) != 0) {
        UartSendStr("ERROR bad weather args\r\n"); return;
    }
    if (t < -40) t = -40; if (t > 50) t = 50;
    g_weather_temp = (int8_t)t;

    cond = skip_spaces(args);
    g_weather_cond = 0;
    if      (scmp(cond, "SUN")) g_weather_cond = 1;
    else if (scmp(cond, "CLD")) g_weather_cond = 2;
    else if (scmp(cond, "OVC")) g_weather_cond = 3;
    else if (scmp(cond, "RAI")) g_weather_cond = 4;
    else if (scmp(cond, "SNO")) g_weather_cond = 5;
    else if (scmp(cond, "FOG")) g_weather_cond = 6;

    /* Generate 7-seg display: "<temp>°C <cond>" e.g. "25°C SUN" or "-5°C SNO" */
    for (i = 0; i < 8; i++) g_weather_buf[i] = 0x00;
    i = 0;
    if (g_weather_temp < 0) {
        g_weather_buf[i++] = 0x40;  /* '-' */
        t = -g_weather_temp;
    } else {
        t = g_weather_temp;
    }
    if (t >= 10) g_weather_buf[i++] = seg7[(uint8_t)(t / 10)];
    g_weather_buf[i++] = seg7[(uint8_t)(t % 10)];
    g_weather_buf[i++] = 0x63;     /* '°' (a,b,g,f) */
    g_weather_buf[i++] = 0x39;     /* 'C' */
    len = (uint8_t)strlen(cond);
    if (len > 3) len = 3;
    for (p = cond; *p && i < 8 && (p - cond) < (int)len; p++)
        g_weather_buf[i++] = CharToSeg7(*p);

    g_weather_valid = 1;
    UartSendStr("OK\r\n");
}

static void CmdSetMode(const char *args)
{
    args = skip_spaces(args);
    if (scmp(args, "NIGHT")) g_mode_night = 1;
    else                     g_mode_night = 0;
    UartSendStr("OK\r\n");
    UpdateClockDisplay();
}

static void CmdGetTime(void)
{
    UartSendStr("OK ");
    if (g_scroll_dir) {
        UartSendDec(seconds % 10); UartSendDec(seconds / 10); UartSendChar('.');
        UartSendDec(minutes % 10); UartSendDec(minutes / 10); UartSendChar('.');
        UartSendDec(hours % 10);   UartSendDec(hours / 10);
    } else {
        UartSendDec(hours);   UartSendChar('.');
        UartSendDec(minutes); UartSendChar('.');
        UartSendDec(seconds);
    }
    UartSendStr("\r\n");
}

static void CmdGetDate(void)
{
    UartSendStr("OK ");
    if (g_scroll_dir) {
        UartSendDec(day);    UartSendChar('.');
        UartSendDec(month);  UartSendChar('.');
        UartSendDec(year);
    } else {
        UartSendDec(year);   UartSendChar('.');
        UartSendDec(month);  UartSendChar('.');
        UartSendDec(day);
    }
    UartSendStr("\r\n");
}

static void CmdGetAlarm(void)
{
    UartSendStr("OK ");
    if (g_scroll_dir) {
        UartSendDec(alarm_seconds % 10); UartSendDec(alarm_seconds / 10); UartSendChar('.');
        UartSendDec(alarm_minutes % 10); UartSendDec(alarm_minutes / 10); UartSendChar('.');
        UartSendDec(alarm_hours % 10);   UartSendDec(alarm_hours / 10);
    } else {
        UartSendDec(alarm_hours);   UartSendChar('.');
        UartSendDec(alarm_minutes); UartSendChar('.');
        UartSendDec(alarm_seconds);
    }
    UartSendChar(' ');
    UartSendStr(alarm_enabled ? "ON\r\n" : "OFF\r\n");
}

static void CmdGetDisplay(void)
{
    UartSendStr("OK ");
    UartSendStr(g_disp_off ? "OFF\r\n" : "ON\r\n");
}

static void CmdGetFormat(void)
{
    UartSendStr("OK ");
    UartSendStr(g_scroll_dir ? "RIGHT\r\n" : "LEFT\r\n");
}

//=============================================================================
// Command line processor — called from main loop
//=============================================================================
static void CmdProcess(void)
{
    static char line[CMD_BUF_SIZE];
    static uint8_t idx = 0;

    while (g_uart_rx_tail != g_uart_rx_head)
    {
        char c = (char)g_uart_rx_buf[g_uart_rx_tail];
        g_uart_rx_tail = (g_uart_rx_tail + 1) % UART_RX_BUF_SIZE;

        /* Only add printable bytes (0x20-0x7E) to line buffer */
        if ((uint8_t)c >= 0x20u) {
            if (idx < CMD_BUF_SIZE - 1)
                line[idx++] = c;
            continue;
        }
        /* CR or LF — process if we have content */
        if (c != 0x0D && c != 0x0A)
            continue;
        if (idx == 0) continue;

        line[idx] = '\0';
        idx = 0;

        /* Trim trailing spaces */
        { int k = (int)strlen(line) - 1;
          while (k > 0 && line[k] == ' ') line[k--] = '\0'; }

        if (line[0] != '*') continue;
        g_evt_suppress = 1;

        /* Dispatch by length + prefix (case-insensitive). */
        { char c1=line[1],c2=line[2],c3=line[3],c4=line[4];
          if (c1>='a'&&c1<='z') c1-=32; if (c2>='a'&&c2<='z') c2-=32;
          if (c3>='a'&&c3<='z') c3-=32; if (c4>='a'&&c4<='z') c4-=32;
        if (c1=='P' && c2=='I' && c3=='N' && c4=='G' && line[5]==0) {
            UartSendStr("*PONG "); UartSendDec(g_ui32SysTickCount / 1000); UartSendStr("\r\n");
        }
        else if (c1=='R' && c2=='S' && c3=='T' && line[4]==0) {
            hours = 0; minutes = 0; seconds = 0;
            year = 2026; month = 6; day = 4;
            alarm_hours = 0; alarm_minutes = 0; alarm_seconds = 0;
            alarm_enabled = 0; g_disp_off = 0; g_mode_night = 0; g_weather_valid = 0; g_weather_cond = 0; g_ntp_state = 0; g_led_override = 0; g_last_ntp_tick = 0;
            g_scroll_dir = 0; g_scroll_active = 0; g_scroll_auto_exit = 0; g_msg_static_until = 0;
            g_disp_mode = DISP_TIME; g_edit_mode = EDIT_NONE;
            UartSendStr("OK\r\n");
            UpdateClockDisplay();
        }
        else if (spfx(line, "*SET:")) {
            char *sub = (char*)skip_spaces(line + 5);
            char *sp  = strchr(sub, ' ');
            const char *arg;
            if (sp) { *sp = '\0'; arg = skip_spaces(sp + 1); }
            else      arg = sub + strlen(sub);

            if      (scmp(sub, "TIME"))    CmdSetTime(arg);
            else if (scmp(sub, "DATE"))    CmdSetDate(arg);
            else if (scmp(sub, "ALARM"))   CmdSetAlarm(arg);
            else if (match_kw(sub, "DISPlay")) CmdSetDisplay(arg);
            else if (scmp(sub, "FORMAT"))  CmdSetFormat(arg);
            else if (scmp(sub, "MSG"))     CmdSetMsg(arg);
            else if (scmp(sub, "BEEP"))    CmdSetBeep(arg);
            else if (scmp(sub, "LED"))     CmdSetLed(arg);
            else if (scmp(sub, "KEY"))     CmdSetKey(arg);
            else if (scmp(sub, "MODE"))    CmdSetMode(arg);
            else if (scmp(sub, "WEATHER") || scmp(sub, "WEA")) CmdSetWeather(arg);
            else UartSendStr("ERROR unknown set\r\n");

            if (sp) *sp = ' ';
        }
        else if (spfx(line, "*GET:")) {
            char *sub = (char*)skip_spaces(line + 5);
            if      (scmp(sub, "TIME"))    CmdGetTime();
            else if (scmp(sub, "DATE"))    CmdGetDate();
            else if (scmp(sub, "ALARM"))   CmdGetAlarm();
            else if (match_kw(sub, "DISPlay")) CmdGetDisplay();
            else if (scmp(sub, "FORMAT"))  CmdGetFormat();
            else UartSendStr("ERROR unknown get\r\n");
        }
        else {
            UartSendStr("ERROR unknown cmd\r\n");
        }
        } /* end case-insensitive block */
        g_evt_suppress = 0;
    }
}
