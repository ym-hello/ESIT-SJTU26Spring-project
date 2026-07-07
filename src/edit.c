// Edit mode increment for S800 clock
#include "edit.h"
#include "alarm.h"

uint8_t g_edit_mode = EDIT_NONE;
uint8_t g_edit_field = 0;
uint8_t g_blink_show = 1;
uint32_t g_blink_timer = 0;
uint32_t g_edit_timeout = 0;
uint32_t g_func_press_tick = 0;
uint8_t g_func_hold_triggered = 0;
uint8_t g_func_entered = 0;
uint32_t g_add_repeat_next = 0;

// Saved originals for cancel-on-timeout
uint16_t sv_year;
uint8_t sv_month, sv_day;
uint8_t sv_hours, sv_minutes, sv_seconds;
uint8_t sv_alarm_h, sv_alarm_m, sv_alarm_s;
uint8_t sv_disp_mode;

void EditIncrement(void)
{
    switch (g_edit_mode)
    {
    case EDIT_TIME:
        if (g_edit_field == 0)
            hours = (hours + 1) % 24;
        else if (g_edit_field == 1)
            minutes = (minutes + 1) % 60;
        else
            seconds = (seconds + 1) % 60;
        break;

    case EDIT_DATE:
        if (g_edit_field == 0)
        {
            year++;
            if (year > 2099) year = 2000;
        }
        else if (g_edit_field == 1)
        {
            month = (month % 12) + 1;
            if (day > DaysInMonth(year, month))
                day = DaysInMonth(year, month);
        }
        else
        {
            day++;
            if (day > DaysInMonth(year, month))
                day = 1;
        }
        break;

    case EDIT_ALARM:
        if (g_edit_field == 0)
            alarm_hours = (alarm_hours + 1) % 24;
        else if (g_edit_field == 1)
            alarm_minutes = (alarm_minutes + 1) % 60;
        else
            alarm_seconds = (alarm_seconds + 1) % 60;
        break;

    default:
        break;
    }
}
