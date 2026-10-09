#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define DIE(s) {perror(s); exit(1);}

int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  int                      status, ignore;
  pid_t                    pid = atoi(argv[1]);

  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  while (1) {  
    // Run untill next syscall Entry
    if (ptrace (PTRACE_SYSCALL, pid, 0, 0) < 0) DIE ("PTRACE_SYSCALL:");
    wait (&status);
    
    ignore = 0;
    if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
    printf ("Entering syscall: %d %d\n", regs.orig_rax, regs.rax);
    if (regs.orig_rax == 101 && regs.rsi == 0) ignore = 1; // PTRACE_TRACEME
    
    // Run until next syscall... should be an exit
    if (ptrace (PTRACE_SYSCALL, pid, 0, 0) < 0) DIE ("PTRACE_SYSCALL:");
    wait (&status);
    if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
    printf ("Exiting syscall: %d %d\n", regs.orig_rax, regs.rax);
    if (ignore) regs.rax = 0;
    if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  }
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
}
