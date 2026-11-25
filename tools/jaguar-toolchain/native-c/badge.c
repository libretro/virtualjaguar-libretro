/* Atari Jaguar Technical Reference Manual, rev. 8, pp. 12–13 and 18–20. */
#define REG16(address) (*(volatile unsigned short *)(address))
#define TOM_VC REG16(0xF00006)
#define TOM_OLP_LOW REG16(0xF00020)
#define TOM_OLP_HIGH REG16(0xF00022)
#define TOM_VMODE REG16(0xF00028)
#define TOM_BG REG16(0xF00058)

#define RGB(red, green, blue) (((red) << 11) | ((blue) << 6) | (green))

enum {
   ART_SIZE = 16,
   SCALE = 4,
   SIZE = ART_SIZE * SCALE,
   ROW_PHRASES = SIZE / 4, /* Four 16-bit pixels per 64-bit phrase. */
   BADGE_X = 128,
   BADGE_Y_HALF_LINE = 190,
   VIDEO_RGB16 = 0x06C7
};

static const char badge[ART_SIZE][ART_SIZE + 1] = {
#include "badge.inc"
};
static unsigned short pixels[SIZE][SIZE] __attribute__((aligned(8)));
static volatile unsigned long objects[6] __attribute__((aligned(16)));

/* Keep a named, executed function for a startup breakpoint. */
void __attribute__((noinline)) drawmark(void)
{
   unsigned int x, y;
   char cell;
   unsigned short color;

   for (y = 0; y < SIZE; ++y)
      for (x = 0; x < SIZE; ++x)
      {
         cell = badge[y / SCALE][x / SCALE];
         color = RGB(2, 9, 7);
         if (cell == '#')
            color = RGB(31, 59, 24);
         else if (cell == '+')
            color = RGB(4, 52, 25);
         pixels[y][x] = color;
      }
}

void sample_main(void)
{
   unsigned long bitmap = (unsigned long)pixels;
   unsigned long list = (unsigned long)objects;
   unsigned long stop = (unsigned long)&objects[4];
   unsigned long bitmap_high = (bitmap << 8) | (stop >> 11);
   unsigned long bitmap_low = (stop << 21) | (SIZE << 14) | (BADGE_Y_HALF_LINE << 3);

   drawmark();
   TOM_BG = RGB(2, 9, 7);
   TOM_VMODE = VIDEO_RGB16;

   /* Bitmap phrase 1, followed by a stop object with CPU interrupt clear. */
   objects[2] = ROW_PHRASES >> 4;
   objects[3] = ((unsigned long)ROW_PHRASES << 28) | (ROW_PHRASES << 18) |
                (1UL << 15) | (4UL << 12) | BADGE_X;
   objects[4] = 0;
   objects[5] = 4;

   for (;;)
   {
      /* The OP writes back DATA and HEIGHT, so re-arm once per field. */
      while ((TOM_VC & 0x07FF) != 0)
         ;
      objects[0] = bitmap_high;
      objects[1] = bitmap_low;
      TOM_OLP_LOW = (unsigned short)list;
      TOM_OLP_HIGH = (unsigned short)(list >> 16);
      while ((TOM_VC & 0x07FF) == 0)
         ;
   }
}
