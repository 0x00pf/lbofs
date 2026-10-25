#include <stdio.h>
#include <time.h>

int main () {
  time_t   t1, t2;

  t1 = time (NULL);
  puts ("Some code here to step over");
  puts ("Some more");
  t2 = time (NULL);
  if (t2 - t1 > 0) {
    printf ("Debugger detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
