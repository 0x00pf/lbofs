#include <stdio.h>
#include <stdint.h>

uint64_t    cksum = 0x92fc; // Precalculated checksum

extern char _text_start[];  // Sumbols from Linker Script
extern char _text_end[];

int text_checksum () {
  uint64_t c = 0;
  for (uint8_t *p = (uint8_t*) _text_start;p < (uint8_t*)_text_end; c += *(p++),c);
  //uint8_t *p = _text_start;
  //for (int i = 0; i < _text_end - _text_start; i++) c += p[i];
  return c;
}

int main () {
  uint64_t c = text_checksum();
  //printf ("%lx %lx (%lx): %x\n", _text_start, _text_end, _text_end-_text_start, c);
  if (c != cksum) {
    printf ("Code was modified\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
