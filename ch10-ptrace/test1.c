#include <stdio.h>
#include <sys/ptrace.h>

int main () {
  printf ("Press a Key\n");
  getchar ();
  if (ptrace (PTRACE_TRACEME,0,0,0) < 0) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");
  return 0;
	    
}
