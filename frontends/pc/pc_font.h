#ifndef PC_FONT_DATA_H
#define PC_FONT_DATA_H
extern const int PC_FONT_W, PC_FONT_H, PC_FONT_CW, PC_FONT_CH, PC_FONT_COLS, PC_FONT_SIZE;
extern const short PC_FONT_ADV[], PC_FONT2_ADV[];   /* per-glyph advance, thousandths of the size */
extern const unsigned char PC_FONT_ALPHA[];   /* 8-bit alpha atlas of the ASCII characters 32..126 at 40 px */
extern const int PC_FONT2_W, PC_FONT2_H, PC_FONT2_CW, PC_FONT2_CH, PC_FONT2_COLS, PC_FONT2_SIZE;
extern const unsigned char PC_FONT2_ALPHA[];   /* the same at 100 px, for large text */
#endif
