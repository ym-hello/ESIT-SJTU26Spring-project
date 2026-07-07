// LED state composition for S800 clock
#include "led.h"
#include "uart.h"
#include "display.h"
#include "alarm.h"
#include "edit.h"
#include "seg7.h"

uint8_t g_led_state;
uint8_t g_led_hb = 0;
uint8_t g_led_override = 0;
uint32_t g_hb_timer = 0;

static uint32_t time_to_sec(uint8_t h, uint8_t m, uint8_t s)
{
    return (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
}

void LedUpdate(void)
{
    uint32_t now = g_ui32SysTickCount;
    uint8_t val;

    if (g_led_override) return;

    val = 0xFF;

    if (g_mode_night) {
        if (g_led_hb) val &= ~(1u << LED_HB_POS);
        g_led_state = val;
        return;
    }

    if (g_led_hb)                     val &= ~(1u << LED_HB_POS);
    if (alarm_enabled && !alarm_ringing) val &= ~(1u << LED_ALM_POS);
    if (g_edit_mode != EDIT_NONE)     val &= ~(1u << LED_EDT_POS);
    if (now - g_tx_led_timer < 100)   val &= ~(1u << LED_UART_POS);
    if (now - g_rx_led_timer < 100)   val &= ~(1u << LED_UART_POS);
    if (g_weather_valid && g_weather_cond == 1) val &= ~(1u << LED_WX_SUN_POS);
    if (g_weather_valid && (g_weather_cond == 4 || g_weather_cond == 5) && g_wx_rain_blink)
                                          val &= ~(1u << LED_WX_RAIN_POS);
    if (g_weather_valid && g_weather_temp >= 30) val &= ~(1u << LED_TEMP_POS);
    if (g_ntp_state == 1 && g_ntp_blink) val &= ~(1u << LED_NTP_POS);
    if (g_last_ntp_tick != 0) {
        uint32_t elapsed_ms = now - g_last_ntp_tick;
        uint32_t elapsed_s = elapsed_ms / 1000;
        uint32_t ntp_sec = time_to_sec(g_last_ntp_h, g_last_ntp_m, g_last_ntp_s) + elapsed_s;
        uint32_t disp_sec = time_to_sec(hours, minutes, seconds);
        uint32_t diff = (ntp_sec > disp_sec) ? (ntp_sec - disp_sec) : (disp_sec - ntp_sec);
        if (diff > 43200) diff = 86400 - diff;
        if (diff <= 2) val &= ~(1u << LED_NTP_POS);
    }

    g_led_state = val;
}
