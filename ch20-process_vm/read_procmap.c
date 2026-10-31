//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h> // PROCMAP_QUERY ioctl

#define DIE(s) {perror(s); exit(1);}

int main (int argc, char *argv[]) {
  int                  fd;
  char                 path[1024];
  char                 name[1024];
  pid_t                pid;
  struct procmap_query q;

  pid = atoi (argv[1]);
  snprintf (path, 1024, "/proc/%d/maps", pid);
  printf ("> Testing %s\n", path);
  if ((fd = open (path, O_RDONLY)) < 0) DIE ("open:");

  memset (&q, 0, sizeof(q));
  q.size = sizeof(q);
  //q.query_flags = PROCMAP_QUERY_COVERING_OR_NEXT_VMA | PROCMAP_QUERY_VMA_EXECUTABLE;
  q.query_flags = PROCMAP_QUERY_COVERING_OR_NEXT_VMA;
  q.vma_name_addr = (long long)name; 
  q.vma_name_size = sizeof(name);

  // Get memory map
  int r;
  while ((r = ioctl (fd, PROCMAP_QUERY, &q)) == 0) {
    printf ("%p-%p %d %08lx %d:%d %ld %s\n", q.vma_start, q.vma_end, q.vma_flags,
	    q.vma_offset, q.dev_major, q.dev_minor, q.inode, name);

    // Prepare next iteration
    memset (name, 0, sizeof(name));
    q.query_addr = q.vma_end;
    q.vma_name_size = sizeof(name);

  }
  return 0;
}
