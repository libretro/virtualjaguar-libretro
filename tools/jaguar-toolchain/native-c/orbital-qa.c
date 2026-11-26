/* Atari Jaguar Technical Reference Manual, rev. 8, pp. 12–13, 18–20, 95–96. */
#define REG16(address) (*(volatile unsigned short *)(address))
#define TOM_VC REG16(0xF00006)
#define TOM_OLP_LOW REG16(0xF00020)
#define TOM_OLP_HIGH REG16(0xF00022)
#define TOM_VMODE REG16(0xF00028)
#define TOM_BG REG16(0xF00058)
#define JOY_ROWS REG16(0xF14000)
#define JOY_BUTTONS REG16(0xF14002)
#define RGB(red, green, blue) (((red) << 11) | ((blue) << 6) | (green))

enum {
   WIDTH = 304, HEIGHT = 188, ROW_PHRASES = WIDTH / 4, STARS = 44,
   SKY_TOP = 54, SKY_HEIGHT = 66, SHIP_SCALE = 2,
   SHIP_WIDTH = 14 * SHIP_SCALE, SHIP_HEIGHT = 9 * SHIP_SCALE,
   HOME_X = (WIDTH - SHIP_WIDTH) / 2, HOME_Y = 83,
   DISPLAY_Y = 84, SKY_END = DISPLAY_Y + (SKY_TOP + SKY_HEIGHT) * 2,
   UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, A = 16, B = 32, C = 64,
   PAUSE = 128, OPTION = 256,
   INK = RGB(2, 5, 8), PANEL = RGB(4, 10, 15), BONE = RGB(29, 56, 26),
   MINT = RGB(5, 60, 23), PINK = RGB(31, 18, 22), SKY_BLUE = RGB(5, 20, 22)
};

static const unsigned char font[26][7] = {
#include "font.inc"
};
static const char ship[9][15] = {
   ".........#....", ".......####...", "..#..###++##..",
   "...###+++++###", "##############", "...###+++++###",
   "..#..###++##..", ".......####...", ".........#...."
};
static unsigned short pixels[HEIGHT][WIDTH] __attribute__((aligned(8)));
static volatile unsigned long objects[6] __attribute__((aligned(16)));

struct Star {
   unsigned short x, y, speed;
};

static unsigned short sky(unsigned int y)
{
   unsigned int band = (y - SKY_TOP) >> 4;
   return RGB(2 + band, 5 + band, 9 + band * 2);
}

static void box(int x, int y, int w, int h, unsigned short color)
{
   int row, column;
   for (row = y; row < y + h; ++row)
      for (column = x; column < x + w; ++column)
         pixels[row][column] = color;
}

static void text(int x, int y, const char *label, int scale, unsigned short color)
{
   int row, column, letter;
   for (; *label; ++label, x += 6 * scale)
   {
      letter = *label - 'A';
      if (letter < 0 || letter >= 26)
         continue;
      for (row = 0; row < 7; ++row)
         for (column = 0; column < 5; ++column)
            if (font[letter][row] & (16 >> column))
               box(x + column * scale, y + row * scale, scale, scale, color);
   }
}

static unsigned int read_pad(void)
{
   unsigned int pad;

   /* Port 1 active-low rows E, D, B, 7. Enable controller output. */
   JOY_ROWS = 0x81FE;
   pad = (~JOY_ROWS >> 8) & 15U;
   if (!(JOY_BUTTONS & 2)) pad |= A;
   if (!(JOY_BUTTONS & 1)) pad |= PAUSE;
   JOY_ROWS = 0x81FD;
   if (!(JOY_BUTTONS & 2)) pad |= B;
   JOY_ROWS = 0x81FB;
   if (!(JOY_BUTTONS & 2)) pad |= C;
   JOY_ROWS = 0x81F7;
   if (!(JOY_BUTTONS & 2)) pad |= OPTION;
   return pad;
}

static void draw_panel(unsigned int state, unsigned int changed)
{
   static const char *const labels[] = {"MOVE", "BOOST", "COLOR", "HOME", "PAUSE", "WARP"};
   static const char *const shortcuts[] = {"DPAD", "A", "B", "C", "", "OPT"};
   static const unsigned int buttons[] = {15, A, B, C, PAUSE, OPTION};
   int i, x, on;

   for (i = 0; i < 6; ++i)
   {
      if (!(changed & buttons[i]))
         continue;
      x = 8 + 49 * i;
      on = (state & buttons[i]) != 0;
      box(x, 153, 44, 24, on ? MINT : PANEL);
      text(x + 5, 156, labels[i], 1, on ? INK : BONE);
      text(x + 5, 165, shortcuts[i], 1, on ? INK : SKY_BLUE);
   }
}

static void draw_scene(void)
{
   int y, column, offset, spacing;

   box(0, 0, WIDTH, HEIGHT, INK);
   box(8, 7, WIDTH - 16, 1, MINT);
   text(64, 16, "ORBITAL QA", 3, BONE);
   text(56, 41, "FLIGHT CHECK   CLEARED FOR FUN", 1, MINT);
   for (y = SKY_TOP; y < SKY_TOP + SKY_HEIGHT; ++y)
      box(0, y, WIDTH, 1, sky(y));
   for (y = 120; y < 146; ++y)
   {
      box(0, y, WIDTH, 1, RGB(8 + ((145 - y) >> 2), 7, 14));
      spacing = (y - 117) >> 1;
      offset = 152 - 7 * spacing;
      for (column = 0; column < 15; ++column, offset += spacing)
      {
         pixels[y][offset] = SKY_BLUE;
      }
   }
   box(0, 120, WIDTH, 1, PINK);
   box(0, 133, WIDTH, 1, PANEL);
   box(0, 144, WIDTH, 1, PANEL);
   box(8, 181, WIDTH - 16, 1, MINT);
   draw_panel(0, 15 | A | B | C | PAUSE | OPTION);
}

void sample_main(void)
{
   struct Star stars[STARS];
   unsigned long bitmap = (unsigned long)pixels;
   unsigned long list = (unsigned long)objects;
   unsigned long stop = (unsigned long)&objects[4];
   unsigned long bitmap_high = (bitmap << 8) | (stop >> 11);
   unsigned long bitmap_low = (stop << 21) | (HEIGHT << 14) | (DISPLAY_Y << 3);
   static const unsigned short colors[] = {MINT, PINK, RGB(31, 46, 9)};
   unsigned int pad = 0, prior = 0, pressed, tick = 0, color = 0;
   unsigned int panel = 0, prior_panel = 0;
   int x = HOME_X, y = HOME_Y, row, column, i, step, paused = 0, warp = 0;
   int star_x = 29, star_row = 11;
   unsigned short shade;

   for (i = 0; i < STARS; ++i)
   {
      stars[i].x = star_x;
      stars[i].y = SKY_TOP + star_row;
      stars[i].speed = 1 + (i & 3);
      star_x += 71;
      if (star_x >= WIDTH) star_x -= WIDTH;
      star_row += 19;
      if (star_row >= SKY_HEIGHT) star_row -= SKY_HEIGHT;
   }
   draw_scene();
   TOM_BG = INK;
   TOM_VMODE = 0x06C7; /* RGB16, four clocks per pixel, composite sync. */
   objects[2] = ROW_PHRASES >> 4;
   objects[3] = ((unsigned long)ROW_PHRASES << 28) | (ROW_PHRASES << 18) |
                (1UL << 15) | (4UL << 12) | 12;
   objects[4] = 0;
   objects[5] = 4;

   for (;;)
   {
      /* Rearm on field wrap. Either parity can start the next field. */
      while ((TOM_VC & 0x07FF) >= SKY_END)
         ;
      objects[0] = bitmap_high;
      objects[1] = bitmap_low;
      TOM_OLP_LOW = (unsigned short)list;
      TOM_OLP_HIGH = (unsigned short)(list >> 16);
      if (panel != prior_panel)
      {
         draw_panel(panel, panel ^ prior_panel);
         prior_panel = panel;
      }

      /* Update the sky after its last scanline, while the panel is scanned. */
      while ((TOM_VC & 0x07FF) < SKY_END)
         ;
      pad = read_pad();
      pressed = pad & ~prior;
      if (pressed & PAUSE) paused = !paused;
      if (pressed & OPTION) warp = !warp;
      if (pressed & B) color = (color == 2 ? 0 : color + 1);
      panel = pad | (paused ? PAUSE : 0) | (warp ? OPTION : 0);
      prior = pad;
      if (paused && !(pressed & (B | C)))
         continue;

      for (row = y; row < y + SHIP_HEIGHT; ++row)
         box(x - 8, row, SHIP_WIDTH + 8, 1, sky(row));
      for (i = 0; i < STARS; ++i)
         pixels[stars[i].y][stars[i].x] = sky(stars[i].y);
      if (pressed & C)
      {
         x = HOME_X;
         y = HOME_Y;
      }
      if (!paused)
      {
         step = pad & A ? 4 : 2;
         x += (pad & RIGHT ? step : 0) - (pad & LEFT ? step : 0);
         y += (pad & DOWN ? step : 0) - (pad & UP ? step : 0);
         if (x < 8) x = 8;
         if (x + SHIP_WIDTH > WIDTH) x = WIDTH - SHIP_WIDTH;
         if (y < SKY_TOP) y = SKY_TOP;
         if (y + SHIP_HEIGHT > SKY_TOP + SKY_HEIGHT)
            y = SKY_TOP + SKY_HEIGHT - SHIP_HEIGHT;
         ++tick;
      }
      for (i = 0; i < STARS; ++i)
      {
         step = stars[i].speed;
         if (warp || (pad & A)) step += step << 1;
         if (!paused)
         {
            star_x = stars[i].x - step;
            stars[i].x = star_x < 0 ? star_x + WIDTH : star_x;
         }
         pixels[stars[i].y][stars[i].x] = (i & 3) == 0 ? BONE : SKY_BLUE;
      }
      for (row = 0; row < 9; ++row)
         for (column = 0; column < 14; ++column)
         {
            if (ship[row][column] == '.') continue;
            shade = ship[row][column] == '#' ? BONE : colors[color];
            box(x + SHIP_SCALE * column, y + SHIP_SCALE * row,
                SHIP_SCALE, SHIP_SCALE, shade);
         }
      step = (pad & A) ? 8 : 2 + (tick & 3);
      box(x - step, y + SHIP_HEIGHT / 2 - 1, step, 2, (tick & 4) ? PINK : colors[color]);
   }
}
