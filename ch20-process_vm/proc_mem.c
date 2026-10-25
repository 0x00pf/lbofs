#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/mman.h>

#define DIE(s) {perror(s); exit(1);}

int inject_syscall (pid_t pid, struct user_regs_struct *regs, int syscall,
		    int p1, int p2, int p3, int p4) {
  int status;
  
  regs->rax = syscall;
  regs->rdi = p1;
  regs->rsi = p2;
  regs->rdx = p3;
  regs->rcx = p4;

  if (ptrace (PTRACE_SETREGS,pid,0,regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);
  return status;
}


int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  char                     *msg = "\x0f\x05Hello, world!\n";
  ssize_t                  len = strlen(msg);
  char                     mem_path[1024], *orig_code;
  int                      mem;
  
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));

  snprintf (mem_path, 1024, "/proc/%d/mem", pid);
  mem = open (mem_path, O_RDWR);

  orig_code = malloc (len);
  pread (mem, orig_code, len, regs.rip);
  
  pwrite (mem, msg, len, regs.rip);
  inject_syscall (pid, &regs, 1, 1, regs.rip + 2, len -2, 0);
  pwrite (mem, orig_code, len, regs.rip);

  close (mem);
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
}
