//#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>

int main (int argc, char *argv[]) {
  printf ("PID: %d\n", getpid());
  void *h = dlopen ("./libtest.so", RTLD_NOW);
  //void *h = dlopen ("./libevil.so", RTLD_NOW);
  if (h == NULL) {
    fprintf (stderr, "Error: %s\n", dlerror());
    return -1;
  }
  void (*f)() = dlsym (h, "run");
  while (1) {
    sleep (1);
    f();
  }
  dlclose (h);
}
