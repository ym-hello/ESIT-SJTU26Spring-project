// Display scan, clock rendering, and scroll control for S800 clock
#include "display.h"
#include "seg7.h"
#include "alarm.h"
#include "edit.h"

uint8_t g_disp_buf[8];
uint8_t g_disp_off = 0;
uint8_t g_mode_night = 0;

const uint8_t *g_pscroll_seg = 0;
const uint8_t *g_pscroll_dp = 0;
uint8_t g_scroll_len = 0;
uint8_t g_scroll_active = 0;
uint8_t g_scroll_pos = 0;
uint8_t g_scroll_dir = 0;
uint8_t g_scroll_speed = 0;
uint32_t g_scroll_timer = 0;
uint8_t g_scroll_auto_exit = 0;
uint8_t g_scroll_cycles = 0;
uint32_t g_msg_static_until = 0;

void ScanDisplay(void)
{
    static uint8_t idx = 0;

    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, g_disp_buf[idx]);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, (uint8_t)(1 << idx));

    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, g_led_state);

    idx = (idx + 1) & 0x07;
}

void ScrollInit(const uint8_t *seg, const uint8_t *dp, uint8_t len)
{
    g_pscroll_seg = seg;
    g_pscroll_dp = dp;
    g_scroll_len = len;
    g_scroll_pos = 0;
    g_scroll_active = 1;
}

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
                seg = g_pscroll_seg[idx];
                if (g_pscroll_dp && g_pscroll_dp[idx])
                    seg |= 0x80;
            }
            else
            {
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

void UpdateClockDisplay(void)
{
    uint8_t i;

    if (g_disp_off) { memset(g_disp_buf, 0x00, 8); return; }

    if (g_msg_static_until && g_edit_mode == EDIT_NONE)
        return;

    if (g_weather_until && g_edit_mode == EDIT_NONE && !g_scroll_active) {
        if (g_weather_valid) {
            uint8_t wi;
            for (wi = 0; wi < 8; wi++)
                g_disp_buf[wi] = g_weather_buf[wi];
        } else {
            g_disp_buf[0] = 0x40;
            g_disp_buf[1] = 0x40;
            g_disp_buf[2] = 0x63;
            g_disp_buf[3] = 0x39;
            g_disp_buf[4] = 0x40;
            g_disp_buf[5] = 0x40;
            g_disp_buf[6] = 0x40;
            g_disp_buf[7] = 0x00;
        }
        return;
    }

    if (g_scroll_active && g_edit_mode == EDIT_NONE)
    {
        ScrollRender();
        return;
    }

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

    if (g_mode_night && g_edit_mode == EDIT_NONE)
    {
        g_disp_buf[4] = 0x00;
        g_disp_buf[5] = 0x00;
        g_disp_buf[6] = 0x00;
        g_disp_buf[7] = 0x00;
    }

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

    if (g_scroll_dir && !g_scroll_active && g_edit_mode == EDIT_NONE) {
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
}
