#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

__attribute__((constructor))
void my_init () {
  //while (1)
    {
      puts ("Init Evil code1");
      fflush (stdout);
  }
}


void run () {
  puts ("Running Evil code");
}
