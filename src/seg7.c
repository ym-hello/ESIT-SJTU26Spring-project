// 7-segment display font and character conversion
#include "seg7.h"

const uint8_t seg7[] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x58, 0x5E, 0x79, 0x71
};

uint8_t CharToSeg7(char c)
{
    if (c >= '0' && c <= '9') return seg7[c - '0'];
    switch (c) {
        case 'A': case 'a': return 0x77; case 'B': case 'b': return 0x7C;
        case 'C': case 'c': return 0x39; case 'D': case 'd': return 0x5E;
        case 'E': case 'e': return 0x79; case 'F': case 'f': return 0x71;
        case 'G': case 'g': return 0x3D; case 'H': case 'h': return 0x76;
        case 'I': case 'i': return 0x30; case 'J': case 'j': return 0x1E;
        case 'K': case 'k': return 0x7A; case 'L': case 'l': return 0x3C;
        case 'M': case 'm': return 0x55; case 'N': case 'n': return 0x37;
        case 'O': case 'o': return 0x3F; case 'P': case 'p': return 0x73;
        case 'Q': case 'q': return 0x67; case 'R': case 'r': return 0x70;
        case 'S': case 's': return 0x6D; case 'T': case 't': return 0x78;
        case 'U': case 'u': return 0x3E; case 'V': case 'v': return 0x7E;
        case 'W': case 'w': return 0x6A; case 'X': case 'x': return 0x36;
        case 'Y': case 'y': return 0x6E; case 'Z': case 'z': return 0x49;
        case '-': return 0x40; case '_': return 0x08; case (char)0xB0: return 0x63;
        case '=': return 0x48; case ' ': return 0x00;
        default:  return 0x00;
    }
}

char SegToChar(uint8_t seg)
{
    uint8_t s7 = seg & 0x7F;
    uint8_t i;
    if (s7 == 0x00) return '_';
    for (i = 0; i < 10; i++) { if (s7 == seg7[i]) return (char)('0' + i); }
    if (s7 == SEG_A) return 'A';
    if (s7 == 0x7C) return 'B';
    if (s7 == 0x39) return 'C';
    if (s7 == 0x5E) return 'D';
    if (s7 == 0x79) return 'E';
    if (s7 == 0x3D) return 'G';
    if (s7 == 0x76) return 'H';
    if (s7 == 0x30) return 'I';
    if (s7 == 0x1E) return 'J';
    if (s7 == 0x7A) return 'K';
    if (s7 == SEG_L) return 'L';
    if (s7 == SEG_M) return 'M';
    if (s7 == SEG_N) return 'N';
    if (s7 == 0x3F) return 'O';
    if (s7 == 0x73) return 'P';
    if (s7 == 0x67) return 'Q';
    if (s7 == 0x70) return 'R';
    if (s7 == 0x6D) return 'S';
    if (s7 == 0x78) return 'T';
    if (s7 == SEG_U) return 'U';
    if (s7 == SEG_V) return 'V';
    if (s7 == 0x6A) return 'W';
    if (s7 == 0x36) return 'X';
    if (s7 == SEG_Y) return 'Y';
    if (s7 == 0x49) return 'Z';
    if (s7 == 0x40) return '-';
    if (s7 == 0x63) return (char)0xB0;
    if (s7 == 0x08) return '=';
    return '?';
}
