#ifndef ALARM_H
#define ALARM_H
#include "board.h"

extern uint8_t alarm_hours, alarm_minutes, alarm_seconds;
extern uint8_t alarm_enabled;
extern uint8_t alarm_ringing;
extern uint32_t alarm_ring_start;
extern uint8_t alarm_buzzer_on;
extern uint32_t alarm_buzzer_timer;
extern uint32_t alarm_led_timer;
extern uint8_t alarm_extra_beeps;
extern uint32_t alarm_extra_timer;
extern uint8_t alarm_hitemp;
extern uint8_t alarm_extra_phase;
#endif
