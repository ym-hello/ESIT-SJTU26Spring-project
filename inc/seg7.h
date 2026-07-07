#ifndef SEG7_H
#define SEG7_H
#include "board.h"

extern const uint8_t seg7[];
uint8_t CharToSeg7(char c);
char SegToChar(uint8_t seg);
#endif
