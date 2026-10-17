#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define DIE(s) {perror(s); exit(1);}

struct picomite_t {
  uint64_t offset;
  uint64_t target1;
  uint64_t target0;
  uint64_t c;
  char     *flags;
};

char *cond[] = {"jne", "jle", "jmp", "jg", "je", NULL};

struct picomite_t pm[] = {
{0x1268, 0x14a5, 0x126d, 4, "e"},
{0x1275, 0x12c6, 0x1276, 2, "mp"},
{0x1289, 0x12c2, 0x128a, 1, "le"},
{0x129d, 0x12c2, 0x129e, 3, "g"},
{0x12cb, 0x1276, 0x12cc, 1, "le"},
{0x1304, 0x14a5, 0x1309, 0, "ne"},
{0x1317, 0x14a5, 0x131c, 1, "le"},
{0x132a, 0x14a5, 0x132f, 3, "g"},
{0x133d, 0x14a5, 0x1342, 1, "le"},
{0x1350, 0x14a5, 0x1355, 3, "g"},
{0x1363, 0x14a5, 0x1368, 1, "le"},
{0x1376, 0x14a5, 0x137b, 3, "g"},
{0x142c, 0x14a5, 0x142d, 0, "ne"},
{0x145c, 0x14a5, 0x145d, 0, "ne"},
{0x146b, 0x14a5, 0x146c, 0, "ne"},
{0x147a, 0x14a5, 0x147b, 0, "ne"},
{0x149d, 0x14a5, 0x149e, 0, "ne"},
{0x14a4, 0x14aa, 0x14a5, 2, "mp"},
{0,0,0,0,NULL}};




int
find_picomite (uint64_t base, uint64_t a) {
  int i;
  for (i = 0; pm[i].offset != 0; i++)
    if (a == (base + pm[i].offset)) return i;
  return -1;
}

int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  pid_t                    pid;
  puts ("picomites runner");

  // Fork
  if ((pid = fork()) < 0) DIE ("fork:");
  if (pid == 0) {
    // child TRACEME
    //if (ptrace (PTRACE_TRACEME,0,0,0) < 0) DIE ("PTRACE_TRACEME");
    if (ptrace (PTRACE_TRACEME) < 0) DIE ("PTRACE_TRACEME");

    execl (argv[1], NULL);
  }
  
  int status;
  char path[1024];
  wait (&status);
  printf ("Tracing %s: %d\n", argv[1], pid);
  // Determine base address from memory map
  snprintf (path, 1024, "/proc/%d/maps", pid);
  FILE *f = fopen (path, "rt");
  uint64_t start, end;
  while (!feof (f)) {
    char buf[1024];
    fgets (buf, 1024, f);

    if (strstr (buf, argv[1]) && strstr (buf, "r--p")) {
      sscanf (buf, "%lx-%lx", &start, &end);
      break;
      //if ((void*)cos > (void*) start && (void*)cos < (void*)end) break;
    }
  }
#ifdef DEBUG
  printf ("Text segment found at: %p %p\n", start, end);
#endif  
  uint64_t base = start;
  fclose (f);
  if (ptrace (PTRACE_CONT,pid,0,0) < 0)  DIE ("PTRACE_SETREGS:");

  while (1) {
    // Wait
    wait (&status);
    if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
#ifdef DEBUG    
    printf ("RIP: (%x) - %p\n",  regs.rip - base, regs.rip);
    printf ("----O---SZ-A-P-C\n");
    printf ("%016b\n", regs.eflags);
#endif    
    int i = find_picomite (base, regs.rip);
    if (i < 0) goto c1;
#ifdef DEBUG        
    printf ("Picomite  %d: COND: %d(%s) TRUE: %x  FALSE: %x\n",
	    i, pm[i].c, cond[pm[i].c], pm[i].target1, pm[i].target0);
#endif    
    // Flags
    // 0123 4567 89AB CDEF
    // C-P- A-ZS ---O
    /*
    int cf = regs.eflags & 0x01;
    int pf = regs.eflags & 0x04;
    int af = regs.eflags & 0x10;
    int zf = regs.eflags & 0x40;
    int sf = regs.eflags & 0x80;
    int of = regs.eflags & 0x800;
    */
    int cf = regs.eflags & 0x01;
    int pf = (regs.eflags >> 2)  & 0x01;
    int af = (regs.eflags >> 4)  & 0x01;
    int zf = (regs.eflags >> 6)  & 0x01;
    int sf = (regs.eflags >> 7)  & 0x01;
    int of = (regs.eflags >> 11) & 0x01;

    uint64_t t0 = pm[i].target0;
    uint64_t t1 = pm[i].target1;
#ifdef DEBUG        
    printf ("C: %d P: %d A: %d Z:%d S:%d O:%d\n", cf,pf,af,zf,sf,of);
#endif
    if (pm[i].c == 0) regs.rip = base +      ( zf == 0              ? t1 : t0); // NE
    else if (pm[i].c == 1) regs.rip = base + ((zf != 0 || sf != of) ? t1 : t0); // LE
    else if (pm[i].c == 2) regs.rip = base + t1;                  // JMP
    else if (pm[i].c == 3) regs.rip = base + ((zf == 0 && sf == of) ? t1 : t0); // G
    else if (pm[i].c == 4) regs.rip = base + (zf != 0               ? t1 : t0); // E
#ifdef DEBUG        
    printf ("Jumping to %x (%p)\n", regs.rip - base, regs.rip);
#endif
    if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
  c1:
#ifdef DEBUG    
    getchar ();
#endif
    if (ptrace (PTRACE_CONT,pid,0,0) < 0)  DIE ("PTRACE_SETREGS:");
    // Patch
    // Continue
  }
}
