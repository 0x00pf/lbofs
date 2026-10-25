#include <stdio.h>
#include <string.h>

int main (int argc, char *argv[], char *env[]) {
  printf ("Press a Key\n");
  char **p;
  getchar ();
  for (p = env; *p != NULL; p++) {
    if (strstr(*p, "LD_PRELOAD")) {
      printf ("LD_PRELOAD detected:\n%s\n. Aborting\n", *p);
      return -1;
    }
  }
  printf ("Programs run normally\n");
  return 0;
	    
}
