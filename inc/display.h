#ifndef DISPLAY_H
#define DISPLAY_H
#include "board.h"

extern uint8_t g_disp_buf[8];
extern uint8_t g_disp_off;
extern uint8_t g_mode_night;

extern const uint8_t *g_pscroll_seg;
extern const uint8_t *g_pscroll_dp;
extern uint8_t g_scroll_len;
extern uint8_t g_scroll_active;
extern uint8_t g_scroll_pos;
extern uint8_t g_scroll_dir;
extern uint8_t g_scroll_speed;
extern uint32_t g_scroll_timer;
extern uint8_t g_scroll_auto_exit;
extern uint8_t g_scroll_cycles;
extern uint32_t g_msg_static_until;
#endif
