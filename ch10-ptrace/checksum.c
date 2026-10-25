#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

int text_checksum (int fd, long start, ssize_t size) {
  uint64_t  c = 0;
  int       n = size;
  uint8_t  *buffer = malloc (size);

  // Read data from file
  lseek (fd, start, SEEK_SET);
  n = read (fd, buffer, size);
  if (n != size) printf("Not completely read\n");
  for (int i = 0; i < size; c += buffer[i++]);
  free (buffer);
  return c;
}

int main (int argc, char *argv[]) {
  if (argc != 4) {
    fprintf (stderr, "usage: %s start size binary\n");
    exit (EXIT_FAILURE);
  }
  int fd = open (argv[3], O_RDONLY);
  uint64_t c = text_checksum(fd, atol (argv[1]), atol(argv[2]));
  printf ("Checksum: %0x\n", c);
  close (fd);
  return 0;
}
