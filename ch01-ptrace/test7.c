#include <stdio.h>
#include <string.h>
#include <unistd.h>


int main () {
  char path[1024];
  FILE *f;
  snprintf (path, 1024, "/proc/%d/comm", getppid());
  if ((f = fopen (path, "rt")) == NULL) return -1;
  fgets (path, 1024, f);
  fclose (f);
  if (!strncmp (path, "gdb",3 ) || !strncmp (path, "lldb",4) ||
      !strncmp (path, "strace", 6)) {
    printf ("Debugger detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
