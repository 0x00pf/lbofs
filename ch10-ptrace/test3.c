#include <stdio.h>
#include <signal.h>

int debugged = 1;

void handler(int sig) {
  debugged = 0;
}

int main () {
  signal (SIGTRAP, handler); 
  __asm__ volatile ("int3");
  
  if (debugged) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
