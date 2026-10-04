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
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  long                     val;

  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));
  regs.rax = 1;
  regs.rdi = 1;
  regs.rdx = 16;
  regs.rsi = 0x402000;
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  if ((val = ptrace (PTRACE_PEEKTEXT,pid,regs.rip)) < 0)  DIE ("PTRACE_PEEKTEXT:");
  //if (ptrace (PTRACE_POKETEXT,pid,regs.rip,val & 0xffffffffffff0000 | 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);
  if (ptrace (PTRACE_POKETEXT,pid,old_regs.rip,val) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  //if (ptrace (PTRACE_CONT,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
	    
}
