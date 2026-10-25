#include <stdio.h>

int main () {
  int r = 0;
  getchar ();
  
  __asm__ volatile (
		    "movq $101, %%rax;"  // Load the ptrace syscall number
		    "movq $0, %%rdi;"    // Request: PTRACE_TRACEME (0)
		    "movq $0, %%rsi;"    // PID: 0
		    //		    "movq $0, %%rdx;"    // Addr: 0
		    //		    "movq $0, %%r10;"    // Data: 0
		    "syscall;"           // Trigger the kernel switch
		    : "=a" (r)      // Output: value of rax goes into the C variable 'result'
		    :                    // No input variables mapped inside the asm template
		    : "rcx", "r11", "rdi", "rsi", "memory" // Clobber list
					 //: "rcx", "r11", "rdi", "rsi", "rdx", "r10", "memory" // Clobber list
		    );
  
  if (r) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  } 
  printf ("Programs run normally\n");
  return 0;
	    
}
