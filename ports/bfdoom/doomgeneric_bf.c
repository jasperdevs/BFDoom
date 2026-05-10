#include <stdint.h>
#include <stdio.h>

#include "doomgeneric.h"

extern int bfhost_poll_key(int *pressed, unsigned char *key);

void DG_Init() {
}

void DG_DrawFrame() {
  putchar('B');
  putchar('F');
  putchar('G');
  putchar('2');

  for (int y = 0; y < DOOMGENERIC_RESY; y++) {
    for (int x = 0; x < DOOMGENERIC_RESX; x++) {
      uint32_t pixel = DG_ScreenBuffer[y * DOOMGENERIC_RESX + x];
      putchar((pixel >> 16) & 255);
      putchar((pixel >> 8) & 255);
      putchar(pixel & 255);
    }
  }
}

void DG_SleepMs(uint32_t ms) {
  (void) ms;
}

uint32_t DG_GetTicksMs() {
  static uint32_t ticks = 0;
  ticks += 28;
  return ticks;
}

int DG_GetKey(int *pressed, unsigned char *key) {
  return bfhost_poll_key(pressed, key);
}

void DG_SetWindowTitle(const char *title) {
  (void) title;
}

int main() {
  char *argv[] = {"bfdoom", "-iwad", "doom1.wad", NULL};

  doomgeneric_Create(3, argv);

  while (1) {
    doomgeneric_Tick();
  }

  return 0;
}
