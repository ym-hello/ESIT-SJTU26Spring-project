// S800 clock main entry, SysTick handler, and state machine
#include "board.h"
#include "buttons.h"
#include "display.h"
#include "led.h"
#include "alarm.h"
#include "edit.h"
#include "uart.h"
#include "seg7.h"
#include "cmd.h"

//=============================================================================
// Global variables — defined here, declared extern in board.h
//=============================================================================
volatile uint32_t g_ui32SysTickCount = 0;
uint32_t ui32SysClock;

uint8_t hours = 0, minutes = 0, seconds = 0;
uint16_t year = 2026;
uint8_t month = 6, day = 4;

disp_mode_t g_disp_mode = DISP_TIME;

uint8_t g_ntp_state = 0;
uint8_t g_ntp_blink = 0;
uint32_t g_ntp_timer = 0;
uint32_t g_ntp_start = 0;
uint32_t g_last_ntp_tick = 0;
uint8_t  g_last_ntp_h = 0, g_last_ntp_m = 0, g_last_ntp_s = 0;

uint8_t g_weather_buf[8];
uint8_t g_weather_valid = 0;
uint32_t g_weather_until = 0;
int8_t g_weather_temp = 0;
uint8_t g_weather_cond = 0;
uint8_t g_wx_rain_blink = 0;
uint32_t g_wx_rain_timer = 0;

uint32_t g_beep_until = 0;

volatile uint8_t g_evt_suppress = 0;
uint32_t g_evt_timer = 0;

state_t g_state = S_ALL_ON;
uint32_t g_state_start_tick;
int g_all_blink_count = 0;

uint16_t sv_year;
uint8_t sv_month, sv_day;
uint8_t sv_hours, sv_minutes, sv_seconds;
uint8_t sv_alarm_h, sv_alarm_m, sv_alarm_s;
uint8_t sv_disp_mode;

//=============================================================================
// main entry
//=============================================================================
int main(void)
{
    ui32SysClock = SysCtlClockFreqSet((SYSCTL_XTAL_16MHZ | SYSCTL_OSC_INT | SYSCTL_USE_OSC), 16000000);

    S800_GPIO_Init();
    S800_I2C0_Init();
    UartInit();

    SysTickPeriodSet(ui32SysClock / 1000 - 1);
    SysTickIntEnable();
    SysTickEnable();

    IntMasterEnable();

    memset(g_disp_buf, 0xFF, 8);
    g_led_state = 0x00;
    g_state = S_ALL_ON;
    g_state_start_tick = g_ui32SysTickCount;
    g_all_blink_count = 0;

    while (1)
    {
        static uint32_t last_tick = 0;
        uint32_t current_tick = g_ui32SysTickCount;

        if (current_tick != last_tick)
        {
            last_tick = current_tick;
            ReadButtons();
            ScanDisplay();
            StateMachineUpdate();
            CmdProcess();
        }
    }
}

//=============================================================================
// SysTick interrupt handler
//=============================================================================
void SysTick_Handler(void)
{
    g_ui32SysTickCount++;
}

//=============================================================================
// Main state machine
//=============================================================================
void StateMachineUpdate(void)
{
    uint32_t now = g_ui32SysTickCount;
    uint32_t elapsed = now - g_state_start_tick;

    switch (g_state)
    {
    case S_ALL_ON:
        if (elapsed >= 500)
        {
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;
            g_state = S_ALL_OFF;
            g_state_start_tick = now;
            g_all_blink_count++;
        }
        break;

    case S_ALL_OFF:
        if (elapsed >= 500)
        {
            if (g_all_blink_count >= 2)
            {
                g_disp_buf[0] = seg7[3];
                g_disp_buf[1] = seg7[1];
                g_disp_buf[2] = seg7[9];
                g_disp_buf[3] = seg7[1];
                g_disp_buf[4] = seg7[0];
                g_disp_buf[5] = seg7[5];
                g_disp_buf[6] = seg7[4];
                g_disp_buf[7] = seg7[2];
                g_led_state = 0x00;
                g_state = S_NUM_ON;
            }
            else
            {
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
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;
            g_state = S_NUM_OFF;
            g_state_start_tick = now;
        }
        break;

    case S_NUM_OFF:
        if (elapsed >= 500)
        {
            g_disp_buf[0] = SEG_L;
            g_disp_buf[1] = SEG_U;
            g_disp_buf[2] = SEG_A;
            g_disp_buf[3] = SEG_N;
            g_disp_buf[4] = SEG_Y;
            g_disp_buf[5] = SEG_M;
            g_disp_buf[6] = 0x00;
            g_disp_buf[7] = 0x00;
            g_led_state = 0x00;
            g_state = S_NAME_ON;
            g_state_start_tick = now;
        }
        break;

    case S_NAME_ON:
        if (elapsed >= 500)
        {
            memset(g_disp_buf, 0x00, 8);
            g_led_state = 0xFF;
            g_state = S_NAME_OFF;
            g_state_start_tick = now;
        }
        break;

    case S_NAME_OFF:
        if (elapsed >= 500)
        {
            g_disp_buf[0] = SEG_V;
            g_disp_buf[1] = seg7[1] | 0x80;
            g_disp_buf[2] = seg7[1];
            g_disp_buf[3] = 0x00;
            g_disp_buf[4] = 0x00;
            g_disp_buf[5] = 0x00;
            g_disp_buf[6] = 0x00;
            g_disp_buf[7] = 0x00;
            g_led_state = 0xFF;
            g_state = S_VER;
            g_state_start_tick = now;
        }
        break;

    case S_VER:
        if (elapsed >= 2000)
        {
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
        // ---- FUNC (SW1): stop alarm, enter edit mode ----
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
                g_func_entered = 1;
                UpdateClockDisplay();
            }
        }

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

        if (!g_sw1_pressed && g_func_press_tick
            && g_edit_mode != EDIT_NONE && !g_func_hold_triggered
            && !g_func_entered)
        {
            g_func_press_tick = 0;
            g_func_entered = 0;
            if (g_edit_mode >= EDIT_ALARM)
            {
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

        // ---- ADD (SW3): increment + auto-repeat ----
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
                g_add_repeat_next = now + 200;
                EditIncrement();
                g_edit_timeout = now + 5000;
                UpdateClockDisplay();
            }
            else if (g_sw3_pressed && now >= g_add_repeat_next)
            {
                g_add_repeat_next = now + 200;
                EditIncrement();
                g_edit_timeout = now + 5000;
                UpdateClockDisplay();
            }
        }

        // ---- SAVE (SW4) ----
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
            if (g_edit_mode == EDIT_ALARM)
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

        // ---- SPEED (SW6) ----
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

        // ---- FORMAT (SW7) ----
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

        // ---- EXT (SW8) ----
        if (g_sw8_edge && g_edit_mode == EDIT_NONE)
        {
            g_sw8_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            SendEvtKey("EXT");
        }

        // ---- USER1 ----
        if (g_user1_edge && g_edit_mode == EDIT_NONE)
        {
            g_user1_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            else { g_ntp_state = 1; g_ntp_timer = now; g_ntp_start = now; g_ntp_blink = 1; }
            SendEvtKey("USER1");
        }

        // ---- USER2 ----
        if (g_user2_edge && g_edit_mode == EDIT_NONE)
        {
            g_user2_edge = 0;
            if (g_scroll_active) { g_scroll_active = 0; g_scroll_auto_exit = 0; UpdateClockDisplay(); }
            else { g_weather_until = now + 5000; UpdateClockDisplay(); }
            SendEvtKey("USER2");
        }

        // ---- Static message timeout ----
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

        // ---- NTP sync blink (500ms) / timeout 10s ----
        if (g_ntp_state == 1)
        {
            if (now - g_ntp_timer >= 500) {
                g_ntp_timer = now;
                g_ntp_blink = !g_ntp_blink;
            }
            if (now - g_ntp_start >= 10000) {
                g_ntp_state = 0;
            }
        }

        // ---- DISP (SW5): cycle format ----
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

        // ---- Time update (always runs) ----
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

            // Alarm trigger check
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
                    alarm_extra_beeps = 6;
                if (g_weather_valid && g_weather_temp >= 30)
                    alarm_hitemp = 1;
                GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, BUZZER_PIN);
                g_led_state = 0xFF;
                UartSendStr("*EVT:ALARM\r\n");
            }
        }

        // ---- Alarm ringing update ----
        if (alarm_ringing)
            AlarmRingingUpdate(now);

        // ---- *SET:BEEP timer ----
        if (g_beep_until && now >= g_beep_until)
        {
            g_beep_until = 0;
            GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
        }

        // ---- Scroll advance (paused in edit) ----
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

        // ---- Rain/snow LED blink (1Hz) ----
        if (now - g_wx_rain_timer >= 500)
        {
            g_wx_rain_timer = now;
            g_wx_rain_blink = !g_wx_rain_blink;
        }

        // ---- Heartbeat toggle (500ms) ----
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

        // ---- Compose LED indicators ----
        if (!alarm_ringing)
            LedUpdate();

        break;

    default:
        break;
    }
}
