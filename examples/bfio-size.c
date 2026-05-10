#include <stdio.h>

static void out(int c) {
  putchar(c);
}

static int in_nibble(void) {
  int c = getchar() & 255;
  if (c == 255) {
    return 0;
  }
  return c - 1;
}

static int in_byte(void) {
  int hi = in_nibble();
  int lo = in_nibble();
  return ((hi & 15) << 4) | (lo & 15);
}

int main() {
  const char *path = "doom1.wad";

  out(255);
  out(0);
  out('B');
  out('F');
  out('I');
  out('O');
  out('S');

  while (*path) {
    out(*path++);
  }
  out(0);

  int a = in_byte();
  int b = in_byte();
  int c = in_byte();
  int size = a | (b << 8) | (c << 16);

  out(size == 4196020 ? 'Y' : 'N');
  out('\n');

  return 0;
}
