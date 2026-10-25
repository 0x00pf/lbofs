#include <stdio.h>
#include <string.h>

int check_tracer () {
  FILE *f = fopen ("/proc/self/status", "r");
  char buffer[1024];
  int  r = 0;
  
  while (!feof(f)) {
    fgets (buffer, 1024, f);
    if (!strncmp(buffer, "TracerPid:", 10)) {
      sscanf (buffer + 11, "%d", &r);
      fclose (f);
      return r;
    }
  }
  fclose (f);
  return -1;
}

int main () {
  getchar();
  if (check_tracer()) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
