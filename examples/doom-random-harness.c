int putchar(int);

#include "../vendor/elvm/libc/_builtin.h"
#include "../vendor/doomgeneric/doomgeneric/m_random.c"

int main() {
  for (int i = 0; i < 8; i++) {
    putchar(M_Random());
  }

  return 0;
}
