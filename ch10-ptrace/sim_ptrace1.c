#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define DIE(s) {perror(s); exit(1);}

int read_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/8 + 1; i++) buf[i] = ptrace (PTRACE_PEEKTEXT, pid, ptr+i);
}

int write_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/8 +1; i++) ptrace (PTRACE_POKETEXT, pid, ptr + i, buf[i]);
}


int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);

  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  while (1) {  
    if (ptrace (PTRACE_SYSCALL, pid, 0, 0) < 0) DIE ("PTRACE_SYSCALL:");
    wait (&status);
    
    if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
    printf ("Entering syscall: %3d \n", regs.orig_rax);
    
    if (ptrace (PTRACE_SYSCALL, pid, 0, 0) < 0) DIE ("PTRACE_SYSCALL:");
    wait (&status);
    
    if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
    if (regs.orig_rax == 0 && regs.rdi != 0) {
      char  *aux, buf[1024];
      read_mem (pid, (uint64_t*)regs.rsi, (uint64_t*)buf, 1024);
      if (aux = strstr (buf, "TracerPid:")) strcpy (aux+11, "0\n");
      write_mem (pid, (uint64_t*)regs.rsi, (uint64_t*)buf, 1024);
    }
    if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  }
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
}
