#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define DIE(s) {perror(s); exit(1);}

int write_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/2 +1; i++) {
    ptrace (PTRACE_POKETEXT, pid, ptr + i, buf[i]);
  }
}

int read_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/2 + 1; i++) {
    buf[i] = ptrace (PTRACE_PEEKTEXT, pid, ptr+i);
  }
}



int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  long                     val;
  char                     *msg = "\x0f\x05Hello, world!\n";
 
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));
  regs.rax = 1;
  regs.rdi = 1;
  regs.rdx = strlen(msg) - 2;
  regs.rsi = regs.rip+2;
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  
  uint64_t *mem, *buf;
  ssize_t  len = strlen(msg);
  
  mem = (uint64_t*)regs.rip;
  buf = malloc (len);
  read_mem (pid, mem, buf, len);

  //if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  write_mem (pid, (uint64_t *)(regs.rip), (uint64_t *)msg, strlen(msg));
  
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);
  write_mem (pid, mem, buf, len);
  free (buf);
  
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
	    
}
