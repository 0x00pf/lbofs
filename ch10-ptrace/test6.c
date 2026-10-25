#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

int main () {
  printf ("%p (%d)\n", main, getpid());
    getchar ();
  if (((uint64_t)main & 0xfffffffffff00000) == 0x555555500000) {
    printf ("gdb or lldb detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
