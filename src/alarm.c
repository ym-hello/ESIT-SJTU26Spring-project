// Alarm state machine and date utilities for S800 clock
#include "alarm.h"
#include "led.h"
#include "uart.h"
#include "display.h"

uint8_t alarm_hours = 0;
uint8_t alarm_minutes = 0;
uint8_t alarm_seconds = 0;
uint8_t alarm_enabled = 0;
uint8_t alarm_ringing = 0;
uint32_t alarm_ring_start = 0;
uint8_t alarm_buzzer_on = 0;
uint32_t alarm_buzzer_timer = 0;
uint32_t alarm_led_timer = 0;
uint8_t alarm_extra_beeps = 0;
uint32_t alarm_extra_timer = 0;
uint8_t alarm_hitemp = 0;
uint8_t alarm_extra_phase = 0;

uint8_t DaysInMonth(uint16_t y, uint8_t m)
{
    if (m == 2)
    {
        if ((y % 400 == 0) || ((y % 4 == 0) && (y % 100 != 0)))
            return 29;
        return 28;
    }
    if (m == 4 || m == 6 || m == 9 || m == 11)
        return 30;
    return 31;
}

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

void AlarmRingingUpdate(uint32_t now)
{
    if (now - alarm_ring_start >= 10000 && !alarm_extra_phase && alarm_extra_beeps > 0)
    {
        alarm_extra_phase = 1;
        alarm_extra_timer = now;
        alarm_buzzer_on = 0;
        GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
    }

    if (now - alarm_ring_start >= 10000 && !alarm_extra_phase)
    {
        alarm_ringing = 0;
        alarm_enabled = 0;
        alarm_hitemp = 0;
        GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
        UartSendStr("*EVT:ALARM_OFF\r\n");
    }

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
