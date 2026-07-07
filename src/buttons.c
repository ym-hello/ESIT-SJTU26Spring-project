// Button debounce and reading for S800 clock
#include "buttons.h"

uint8_t g_sw1_pressed = 0, g_sw1_edge = 0;
uint8_t g_sw2_pressed = 0, g_sw2_edge = 0;
uint8_t g_sw3_pressed = 0, g_sw3_edge = 0;
uint8_t g_sw4_pressed = 0, g_sw4_edge = 0;
uint8_t g_sw5_pressed = 0, g_sw5_edge = 0;
uint8_t g_sw6_pressed = 0, g_sw6_edge = 0;
uint8_t g_sw7_pressed = 0, g_sw7_edge = 0;
uint8_t g_sw8_pressed = 0, g_sw8_edge = 0;
uint8_t g_user1_pressed = 0, g_user1_edge = 0;
uint8_t g_user2_pressed = 0, g_user2_edge = 0;

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
        uint8_t debounced = !raw_bit;
        if (debounced && !*p_pressed)
            *p_edge = 1;
        *p_pressed = debounced;
    }
}

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

    raw = (uint8_t)GPIOPinRead(USER1_PORT, USER1_PIN | USER2_PIN);
    DebounceOne((raw & USER1_PIN) ? 1 : 0, &g_user1_pressed, &g_user1_edge, &cu1, &lu1);
    DebounceOne((raw & USER2_PIN) ? 1 : 0, &g_user2_pressed, &g_user2_edge, &cu2, &lu2);
}
