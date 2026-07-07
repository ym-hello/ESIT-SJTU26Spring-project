// Serial command processing for S800 clock
#include "cmd.h"
#include "serial.h"
#include "seg7.h"
#include "display.h"
#include "led.h"
#include "alarm.h"
#include "edit.h"
#include "buttons.h"

static uint8_t g_msg_segs[32];

//=============================================================================
// String utilities
//=============================================================================
static const char *skip_spaces(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

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

static const char *skip_kw(const char *p)
{
    while (*p && *p != ' ' && *p != '\t' && *p != '\0')
        p++;
    return p;
}

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

static uint8_t hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

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
// EVT senders
//=============================================================================
void SendEvtDisp(void)
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

void SendEvtLed(void)
{
    UartSendStr("*EVT:LED ");
    UartSendHexByte(g_led_state);
    UartSendStr("\r\n");
}

void SendEvtKey(const char *name)
{
    UartSendStr("*EVT:KEY ");
    UartSendStr(name);
    UartSendStr("\r\n");
}

void SendEvtEditSave(void)
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
// Command handlers
//=============================================================================
static void CmdSetTime(const char *args)
{
    uint32_t v;
    const char *next, *p;
    int ok = 0;

    p = skip_spaces(args);
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
        static uint8_t msg_dp[32];
        for (i = 0; i < len; i++) {
            msg_dp[i] = (g_msg_segs[i] & 0x80) ? 1 : 0;
            g_msg_segs[i] &= 0x7F;
        }
        ScrollInit(g_msg_segs, msg_dp, len);
        g_scroll_auto_exit = 1;
        g_scroll_cycles = 0;
    } else {
        for (i = 0; i < len; i++)
            g_disp_buf[i] = g_msg_segs[i];
        for (i = len; i < 8; i++)
            g_disp_buf[i] = 0x00;
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
        LedUpdate();
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
        ;
    UartSendStr("OK\r\n");
}

static void CmdSetWeather(const char *args)
{
    uint8_t i, len;
    int32_t t;
    const char *p, *cond;

    args = skip_spaces(args);
    p = args;

    if (p[0] && p[1] && (((p[0]>='0'&&p[0]<='9')||(p[0]>='A'&&p[0]<='F')||(p[0]>='a'&&p[0]<='f')) &&
                         ((p[1]>='0'&&p[1]<='9')||(p[1]>='A'&&p[1]<='F')||(p[1]>='a'&&p[1]<='f'))))
    {
        int is_hex = 1;
        for (i = 0; p[i] && i < 16; i++)
            if (!((p[i]>='0'&&p[i]<='9')||(p[i]>='A'&&p[i]<='F')||(p[i]>='a'&&p[i]<='f')))
                { is_hex = 0; break; }
        if (is_hex && i == 16) {
            for (i = 0; i < 8; i++) {
                g_weather_buf[i] = (hex_val(p[0]) << 4) | hex_val(p[1]);
                p += 2;
            }
            g_weather_valid = 1;
            UartSendStr("OK\r\n");
            return;
        }
    }

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

    for (i = 0; i < 8; i++) g_weather_buf[i] = 0x00;
    i = 0;
    if (g_weather_temp < 0) {
        g_weather_buf[i++] = 0x40;
        t = -g_weather_temp;
    } else {
        t = g_weather_temp;
    }
    if (t >= 10) g_weather_buf[i++] = seg7[(uint8_t)(t / 10)];
    g_weather_buf[i++] = seg7[(uint8_t)(t % 10)];
    g_weather_buf[i++] = 0x63;
    g_weather_buf[i++] = 0x39;
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
void CmdProcess(void)
{
    static char line[CMD_BUF_SIZE];
    static uint8_t idx = 0;

    while (g_uart_rx_tail != g_uart_rx_head)
    {
        char c = (char)g_uart_rx_buf[g_uart_rx_tail];
        g_uart_rx_tail = (g_uart_rx_tail + 1) % UART_RX_BUF_SIZE;

        if ((uint8_t)c >= 0x20u) {
            if (idx < CMD_BUF_SIZE - 1)
                line[idx++] = c;
            continue;
        }
        if (c != 0x0D && c != 0x0A)
            continue;
        if (idx == 0) continue;

        line[idx] = '\0';
        idx = 0;

        { int k = (int)strlen(line) - 1;
          while (k > 0 && line[k] == ' ') line[k--] = '\0'; }

        if (line[0] != '*') continue;
        g_evt_suppress = 1;

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
