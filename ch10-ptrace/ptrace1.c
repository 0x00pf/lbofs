#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

int main (int argc, char *argv[]) {
  int status;
  if (ptrace (PTRACE_ATTACH,atoi(argv[1]),0,0) < 0) {
      perror ("PTRACE_ATTACH:");
      return -1;
    }
  //wait (&status);
  sleep (5);
  if (ptrace (PTRACE_DETACH,atoi(argv[1]),0,0) < 0) {
      perror ("PTRACE_ATTACH:");
      return -1;
    }

  return 0;
	    
}
