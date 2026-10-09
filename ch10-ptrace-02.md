## Attaching to processes

`ptrace` also allows us to start debugging a running process. In this case, the target process won't call `PTRACE_TRACEME` (unless it is trying to avoid debugging), so we have to use some new `ptrace` commands: `PTRACE_ATTACH` and `PTRACE_SEIZE`. Both allow us to take control of a running program, but they work in slightly different ways.

To illustrate the use of these `ptrace` commands, let’s write a simple program that just counts to 255. So we can easily see if it is stopped or not. I'll write it in Assembly because it will be useful to illustrate some of the coming sections. We’ll also use it in the coming chapters, and having this simple program will make things easier.

I’ll name the program `count` because, you know, it counts. This is the code:

```asm
	global _start

	section .text
_start:
	mov rax, 1
	mov rdi, 1
	mov rsi, msg
	mov rdx, msg_len
	syscall

	xor rbx,rbx
b0:	
	mov r8, rbx
	and r8, 0xF0
	shr r8, 4
	call print_hex	

	mov r8, rbx
	and r8, 0x0F
	call print_hex

	mov r8, 16
	call print_hex
	
	mov rax, 35
	lea     rdi, [rel delay]
    xor     rsi, rsi
	syscall

	inc rbx
	and rbx, 0xFF	
	jmp b0
	
print_hex:
	mov rax, 1
	mov rdi, 1
	mov rdx, 1
	lea rsi, [tbl + r8]
	syscall
	ret
	
section .data
	msg	       db "Counter Program", 0x0a
	msg_len	   EQU $-msg
	tbl	       db "0123456789ABCDEF", 0x0a
    delay      dq 1
    delay_nsec dq 0	
```

The program shows a message and then runs an infinite loop using register `rbx` as a loop counter. The value of `rbx` is printed in hexadecimal on each iteration. Then, the program calls `nanosleep` to wait for a second and repeats the process again in an infinite loop. The program just uses the `write` syscall (`1`) for printing and `nanosleep` (`35`) for waiting.

You can compile this program the usual way with:

    $ nasm -f elf64 -o count.o count.asm
    $ ld -o count count.o

Now, let’s write another small program that attaches to a running process and we’ll indeed use our brand-new `count` program as a target for this test. Let’s first take a look to this program which uses the `PTRACE_ATTACH` command.

```C
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

int main (int argc, char *argv[]) {
  int status;
  if (ptrace (PTRACE_ATTACH,atoi(argv[1]),0,0) < 0) {
      perror ("PTRACE_ATTACH:");
      return -1;
    }
  wait (&status);
  sleep (5);
  if (ptrace (PTRACE_DETACH,atoi(argv[1]),0,0) < 0) {
      perror ("PTRACE_DETACH:");
      return -1;
    }

  return 0;	    
}
```

This program expects the PID of the process to attach to as its first command-line argument. Let’s now launch the `count` program in a different terminal and leave it running there. In another terminal, we’re going to run our `ptrace` example, but first, we need to get the PID of `count`.

    $ ps ax| grep "[c]ount"
    3803134 pts/52   S+     0:28 ./count

The `"[c]ount"` is a trick to avoid seeing the `grep count` command again and again. The regular expression `[c]ount` actually matches `count` as `[c]` means: match a character that is a c. However, the `grep` reported by `ps` will be `grep "[c]ount"`, which has two more characters and doesn’t match.

There’s also a `pgrep` utility that does this; it will return the PID given a process name. It provides some interesting flags so you can find processes with a specific name that belong to a specific user. This tool is indeed very convenient but may not be available on all systems, especially when looking into embedded or IoT devices where only the bare minimum programs are provided. So, it’s good to know that it exists, and it’s also good to know what to do when it isn’t available.

Now that we’ve got the PID, let’s run our `ptrace1` example:

    $ ptrace1 3803134

or in one single step	

    $ ptrace1 `pgrep count`
	
You will see the `count` program frozen for 5 seconds and then continuing its endless counting.

Our program makes use of the `PTRACE_ATTACH` and `PTRACE_DETACH` commands. The first one gets the control of the indicated `pid` and stops it. Actually, it’s more like requesting the control and then using `wait` to wait until it gets the control of the process. That’s why we need to use `wait`; to make sure the target process is in the _stopped_ state. Actually, we have to literally wait for the process to get into the so-called stopped state.

> **PROCESS STATES AND `PTRACE_ATTACH`**

> Processes, or more accurately, tasks as they’re referred to in the kernel, may be in different states, and the transitions between them are not trivial. But we can go with a simplified view of the scheduler (the part of the kernel that takes care of scheduling processes/tasks) focusing on three main states: RUNNING, INTERRUPTIBLE, and STOPPED. A task in the RUNNING state is ready to be executed; it doesn’t mean that it is actually running on the processor right now; it just means that it can be chosen by the scheduler to be executed next. A task is in the INTERRUPTIBLE state usually while waiting for a system call or I/O operation (which is done also by a system call). Tasks in this state can be woken up by the kernel as a result of some interrupt (a signal received, I/O operation completed, a timer timing out, ...).
>
> There are also ZOMBIE and END states that are fired when a process ends, and some other specific states, but those aren’t relevant for our current discussion. Be free to check `include/linux/sched.h` to see all defined states.
>
> In most cases, programs are switching between RUNNING and INTERRUPTIBLE states as their execution progresses. The STOPPED state is reached when SIGSTOP is sent. The task is stopped but still active and can restart execution using a SIGCONT signal. But Linux differentiates between being stopped by SIGSTOP and being stopped by `ptrace`, and the latter state is known as TRACED. In practice, STOPPED and TRACED are the same, and actually `PTRACE_ATTACH` sends SIGSTOP to the process but instead of putting the task in STOPPED mode, it puts it in TRACED mode.
>
> Even when the state of a process is actually a bitmask in a kernel structure, the kernel has to do some more things than flipping some bits. Without going into too much detail, the kernel defines some points where it’s safe to change a process state. This is usually at the exit of a system call when, whatever the process was doing in the kernel is complete, and the kernel is in a safe state. This prevents deadlocks (stopping a process that has blocked a resource preventing other processes to use it) and also solves synchronization issues with multicore processors. Summing up, `PTRACE_ATTACH` tells the kernel: _“whenever you think it’s safe, please put this process in TRACED mode and let me know when that happens.”_ This is the reason why you need to run `wait` after `PTRACE_ATTACH`.

The second command, `PTRACE_DETACH`, releases the control of the process (stops tracing it) and lets it continue its execution normally. Super easy.

The second option to attach to a process is using `PTRACE_SEIZE`. This command allows us to start tracing a process but without stopping it, in other words, the program keeps running normally until we say otherwise. The next program shows how to use this command:

```C
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

#define DIE(s) {perror(s); exit(1);}

int main (int argc, char *argv[]) {
  int status;
  pid_t pid = atoi(argv[1]);
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  sleep (5);
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE (PTRACE_DETACH);

  return 0;	    
}
```

If you compile and run this program against your example, you will observe two things. First, the `count` program will not stop at all. Second, when the `ptrace` program finishes, you’ll see an error:

    PTRACE_DETACH:: No such process

That doesn’t mean that the process is gone (dead or something) or that it wasn’t traced. It means that the process wasn’t in the right state to accept the `PTRACE_DETACH` command. In other words, to issue a `PTRACE_DETACH` command, the traced process must be stopped. When a process is controlled using `PTRACE_SEIZE`, we can use `PTRACE_INTERRUPT` to interrupt the process and stop it, and `PTRACE_CONT` to restart its execution. Adding a `PTRACE_INTERRUPT` after `PTRACE_SEIZE` is equivalent to issuing `PTRACE_ATTACH`; it will ask the kernel to stop the process whenever it’s safe, and the tracer process can use `wait` to be notified of that event. `PTRACE_CONT` works for any process stopped, so you can also use it after `PTRACE_ATTACH` to restart the process execution. Basically, `PTRACE_CONT` works like `PTRACE_DETACH` but the process continues to be traced.

So, if we add a call to `PTRACE_INTERRUPT` after `PTRACE_SEIZE`, we achieve the same result that issuing a `PTRACE_ATTACH` operation. Check the box above about process states to get the process in the TRACED state; the kernel cannot stop the target process immediately; it has to wait to reach a safe point, and that’s what `PTRACE_INTERRUPT` followed by a `wait` does.

## Controlling Registers

Attaching to a running process is the first step to be able to manipulate it. Now, we need to know which kind of process manipulations are possible using `ptrace`, and maybe one of the most useful manipulations you can do is to read and modify the target process registers. For doing that, you can use `PTRACE_GETREGS` and `PTRACE_SETREGS` to get and set the process registers. These two commands can only be issued when the process is in a stopped state.

Remember that our `count` program keeps its counter on register `RBX`, so, let’s write a program to reset the counter at run-time.

```C
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define DIE(s) {perror(s); exit(1);}

int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);

  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);
  if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
  regs.rbx = atoi (argv[2]);
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");

  return 0;
}

```

I bet you’ve found this surprisingly easy. Just read the registers, change them, and write them back. We pass a second argument to the program above with the value we want to assign to the counter. The value is just converted into a number using the `atoi` function and assigned to register `rbx` that, if you remember, was the register we used to keep the counter in our `count` program.

Maybe you’ve already noticed it, but if not, there we go. We can use this to run syscalls directly in the target process. Let’s now write a program that will force the target process to print again the initial `count` message: _"Counter Program"_ every time we run it. In the next section, we’ll see how to print any string or do more interesting things, but for doing that, we need another `ptrace` command that we’ll learn in the next section.

To make the program print the initial message, we need some information from the process. Specifically, the pointer to the string we want to print. There are different ways of doing that. In this case, we’ll just dump the beginning of `_start` that contains the code that prints that string and that is the code we just need to duplicate.


    $ objdump -d count | grep -A11 count
    count:     file format elf64-x86-64


    Disassembly of section .text:

    0000000000401000 <_start>:
      401000:	b8 01 00 00 00       	mov    $0x1,%eax
      401005:	bf 01 00 00 00       	mov    $0x1,%edi
      40100a:	48 be 00 20 40 00 00 	movabs $0x402000,%rsi
      401011:	00 00 00
      401014:	ba 10 00 00 00       	mov    $0x10,%edx
      401019:	0f 05                	syscall

So, what we need to do is the following:

*   Make a copy of the current registers so we can restore execution state after running our system call.
*   Update the registers shown by `objdump` with the values shown by `objdump`.
*   Make `RIP` point to the syscall instruction (that is `0x401019`).
*   Run a single instruction.
*   Restore initial registers.
*   Let the program keep running normally.

The code for doing this is as follows:

```C
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

  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));
  regs.rax = 1;         // write syscall
  regs.rdi = 1;         // stdout
  regs.rdx = 16;        // string len
  regs.rsi = 0x402000;  // string
  regs.rip = 0x401019;  // Address containing a syscall instruction
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);

  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_CONT,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
}

```

> **TECHNIQUE:** Inject syscall using `ptrace` reusing existing `syscall` instruction**
>
> _ Run-Time Code Injection_
>
> Using `ptrace` to attach to a running process allows the execution of many system calls just by properly setting the values of registers and running a `SINGLE_STEP` command. `RIP` needs to be set to the address of a `syscall` instruction.
>

The code is pretty much self-explanatory. Just note the `wait` every time we have to transition from running to stopped, what happens with the first `PTRACE_INTERRUPT` and then with `PTRACE_SINGLESTEP`. Yes, we’ve introduced a new `ptrace` command, but as you can see, there isn’t much to say. `PTRACE_SINGLESTEP` just runs a single instruction. The process needs to be stopped before issuing this command, and we have to use `wait` to gain control back. That’s it.

### `PTRACE_SINGLESTEP` vs Trap Flag

Intel processors provide a flag known as the _Trap Flag_ that is bit 8 in the `eflags/rflags` register that contains all processor flags. The `e` vs `r` comes from the register naming which for intel 32 bits is `eSomenthing` (like `eax` or `esp`) while for 64 bits is `rSomething` (like `rax` or `rsp`). As it happens with the regular registers, the lower bits of both `eflags` and `rflags` are the same, and you can use one or the other interchangeably (unless you really need to access bits from 32 to 63). Anyways, the _Trap Flag_, when enabled, forces an interrupt every time an instruction is completed, effectively enabling step-by-step execution mode, common on debuggers. You can check and enable this flag by accessing the `eflags` registry, which you can get from `PTRACE_GETREGS/SETREGS`, and when enabled, your program will start to get a `SIGTRAP` after each instruction execution.

This is indeed the underlying mechanism used by `PTRACE_SINGLESTEP` on intel processors. The difference is that `PTRACE_SINGLESTEP` does a few more things and, in general, you’d prefer to use it. `PTRACE_SINGLESTEP` will take care of signal management and keep state consistent between calls. It’s also the portable way of doing this that will make your program work OK independently of the platform you compile it for.

However, the main difference is that `PTRACE_SINGLESTEP` is intended to execute a single instruction in a different process using the `ptrace` system call, while using the TF flags will allow us to trace ourselves without using `ptrace`. It can also be used with other processes, but that isn't as straightforward as using `PTRACE_SINGLESTEP`. Note that tracing ourselves may be interesting for some special cases, such as instruction-by-instruction crypters, which decrypt and encrypt program instructions as they get executed, keeping the code encrypted always in memory, except for the current instruction being executed.

You can enable the trap flag and start tracing yourself (or use `PTRACE_GETREGS` and `PTRACE_SETREGS` in a remote process) with code like this:

```C
#include <signal.h>
#include <stdio.h>

static void trap(int sig)
{
    puts("single-step trap!");
}

int main(void)
{
    signal(SIGTRAP, trap);

    asm volatile(
        "pushfq\n"
        "orq $0x100, (%%rsp)\n"
        "popfq\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        :
        : "memory"
    );

    asm volatile(
        "pushfq\n"
        "andq $~0x100, (%%rsp)\n"
        "popfq\n"
        :
        :
        : "memory"
    );

    return 0;
}
```
The first three assembly lines push the flags into the stack (`pushfq`) and then set the TF flag in the word at the top of the stack. After that, it just restores the flag from the stack (`popfq`). Next, we’ll run three `nop`s and then clear the flag.

For this program, in total, you’ll see six calls to the signal handler, one for each of the following instructions:

    pushfq
    orq $0x100, (%%rsp)
    popfq  <---------------------- After this instruction, Flag is 1
    nop    <---------------------- SIGTRAP
    nop    <---------------------- SIGTRAP
    nop    <---------------------- SIGTRAP
    pushfq
    andq $~0x100, (%%rsp ) <------ SIGTRAP
    popfq <----------------------- SIGTRAP. After this instruction, Flag is 0

We’ll use the `PTRACE_SINGLESTEP` many times in the book as a way to inject single instructions in running processes, so you’ll become very familiar with this `ptrace` command.

The use of the TF flag may be interesting for doing some debugging without using `ptrace`; however, there are better techniques, and it’s also really Intel-specific, so for other platforms, we’ll have to fall back on these other techniques anyway. We’ll talk about them later as we need to progressively introduce some concepts before, so you, my beloved reader, don’t get overwhelmed at chapter 1 and stop reading too early :). Anyhow, knowing it exists is always a good thing. You never know when it may be useful.

<!---
NOTE: Activating TF flag on a ptaced file with SETREGS will produce traps in the tracing process
--->


## Modifying memory

The next main operations `ptrace` can do are read and write process memory. For that purpose, the commands `PTRACE_POKETEXT` and `PTRACE_PEEKTEXT` are provided. There are also `PTRACE_XXXDATA` but on Linux it does the same. Initially, there was envisioned to have different commands to write/read code and data, but nowadays all of them are equivalent.

> **NOTE**
>
> There is also `PTRACE_PEEKUSER/PTRACE_POKEUSER`. These allow us to access the tracee's USER area that contains all processor registers. For example, in order to use hardware breakpoints, it’s necessary to set some special registers that are not available using `PTRACE_GETREGISTERS`. These commands are kind of legacy, and modern implementations are encouraged to use `PTRACE_GET/SETREGSET`. The main reason is that the so-called USER area is heavily platform/kernel specific.

We can now rewrite our previous program to print the string without changing `rip` to point to a syscall instruction; we can just write the `syscall` instruction ourselves right where we need it, that is, at the current position of `rip` so we don't have to set it. Something like this:

```{#lst:code .C caption="Program to print arbitrary strings using `ptrace`"}
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
  if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);
  if (ptrace (PTRACE_POKETEXT,pid,old_regs.rip,val) < 0)  DIE ("PTRACE_POKETEXT:");
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  
  return 0;
}

```

For those of you working with different platforms, the table below summarizes the instructions and machine code for invoking system calls for the main platforms, including the hex values you need to poke into memory:

Architecture	ASM instruction	  Machine-code   POKE
------------    ---------------   ------------   ----------
x86 (32-bit)	`int 0x80`	      `CD 80`	     `0x80cd`
x86-64	        `syscall`	      `0F 05`	     `0x050f`
ARM32 	        `svc #0`	      `EF 00 00 00`	 `0xEF000000`
Thumb	        `svc #0`	      `DF 00`	     `0x00df`
Thumb-2	        `svc #0`	      `DF 00`	     `0x00df`
AArch64         `svc #0`	      `01 00 00 D4`	 `0xD4000001`
MIPS32	        `syscall`	      `00 00 00 0C`	 `0x0000000C`
MIPS64	        `syscall`	      `00 00 00 0C`	 `0x0000000C`
RISC-V	        `ecall`	          `73 00 00 00`	 `0x00000073`

If you are working with any of those platforms, you may have to change the `POKETEXT` value in our example program above.

Unfortunately, these commands (`PTRACE_PEEKTEXT` and `PTRACE_POKETEXT`) only allow us to read or write 8 bytes each time (actually a word). Let’s write two helper functions so we can easily write and read arbitrary amounts of data.

```C
int write_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/8 +1; i++) {
    ptrace (PTRACE_POKETEXT, pid, ptr + i, buf[i]);
  }
}

int read_mem (pid_t pid, uint64_t *ptr, uint64_t *buf, size_t size) {
  for (int i = 0; i < size/8 + 1; i++) {
    buf[i] = ptrace (PTRACE_PEEKTEXT, pid, ptr+i);
  }
}
```

_NOTE: For the sake of simplicity, I’m just copying over qwords; however, note that we may be writing/reading some extra bytes in the last qword, which, depending on the specific situation, may cause a buffer overflow or bug. Be free to fix the implementation as an exercise._

Just two regular loops to read and write memory areas in 8-byte blocks. Now, we can modify our previous program and make it print any string we want. This is the complete `main` function, using our new `read_mem` and `write_mem` power functions.

```C
int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  long                     val;
  char                     *msg = "Hello, world!\n";  // NEW
 
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));
  regs.rax = 1;
  regs.rdi = 1;
  regs.rdx = strlen(msg);           // MODIFIED
  regs.rsi = regs.rip + 2;          // MODIFIED
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  
  // NEW CODE
  uint64_t *mem, *buf;
  ssize_t  len = 2 + strlen(msg);
  
  mem = (uint64_t*)regs.rip;
  buf = malloc (len);
  read_mem (pid, mem, buf, len);
  // END OF NEW CODE

  if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  write_mem (pid, (uint64_t *)(regs.rip + 2), (uint64_t *)msg, strlen(msg)); // NEW
  
  if (ptrace (PTRACE_SINGLESTEP,pid,0,0) < 0)  DIE ("PTRACE_SINGLESTEP:");
  wait (&status);
  write_mem (pid, mem, buf, len); // NEW
  free (buf);                     // NEW
  
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  
  return 0;
}
```

> **TECHNIQUE:** Inject syscall with buffers, using `ptrace` at current IP
> _Run-Time Code Injection_
>
> Using `ptrace` to attach to a running process allows the execution of many system calls just by properly setting the values of registers and writing buffer contents into memory after running a `SINGLE_STEP` command.
>
> This technique injects the code right at `RIP`. Buffers can alternatively be written in the stack. That requires two write operations.

We’ve added a new variable named `msg` that contains the message we want to print. We’re using the `write` system call for demonstration purposes, but it can be an `exec` or `open`, and our buffer will contain a path. Then, we need to adjust the values of our registers to account for the size that is stored in register `rdx` and the memory address where the string is stored, which we’re going to do at `rip + 2`. This address is just after the `syscall` instruction (2 bytes) that we’ll insert in the current `rip` position.

But before doing that, we read the memory we’re going to overwrite, so it can be restored afterward and the program can continue with its normal execution. This is what the new code does. It allocates a buffer to hold the original values of the memory that we’ll overwrite and stores it so we can restore them afterward.

Now, we can just inject our `syscall` instruction and copy our message just after the instruction. This leads us to the following memory layout:

    RIP     --> 0x0f              <-- syscall instruction
	            0x05
    RIP + 2 --> Hello, world\n    <-- msg

All set up, it’s time to single step the injected syscall to get the target program to print our new message.

Finally, we have to leave everything as it was before we started messing around with the memory, meaning that we have to copy back the original memory we overwrote and set the registers to their original values so the program can continue execution from where we interrupted it.

Note that in this example, I intentionally left the _poke_ (memory write) of the `syscall` instruction explicit, but we could just copy the `syscall` opcode in the string we want to write.

These are the changes required (I will just show the lines that need changes):

```C
  char                     *msg = "\x0f\x05Hello, world!\n";
  
  regs.rdx = strlen(msg) - 2;
  regs.rsi = regs.rip + 2;
  
  write_mem (pid, (uint64_t *)(regs.rip), (uint64_t *)msg, strlen(msg));
```

These changes make the code a little smaller, but what is more interesting is that this opens the door to injecting generic shellcode. In this example, we’ve packed our code and data together and manage it as a whole.

In this example, I explicitly wrote our data into the `.text` segment to illustrate the process of reading part of a program, modifying it, and then restoring it. We’ll see examples of why you would do this in the next section. For this simple case, we could just copy the string to the stack (using the `rsp` register) and use that pointer. This way, we don’t need to store and restore any code, but its pedagogical value is much less.

This is maybe the simplest way of code injection you can do. In next chapter we'll explore further additional techniques.


## Signal processing with `ptrace`

So far in all our examples, I was assuming that the target process will stop because of a `SIGTRAP`; however, in reality, a process can fire a `wait`/`waitpid` in the tracer program for multiple reasons:

*   The process may have exited normally. It just finished its execution.
*   The process may have died for/by a signal. It may have got a `SIGKILL` from another process.
*   The process may have stopped. This is what we’ve been supposing so far; however, the process may have entered the stopped state for different reasons:
    *   Process stopped because it hit a system call after using the `PTRACE_SYSCALL` command.
    *   Process stopped because it hit a breakpoint.
    *   Process stopped because it was started using `PTRACE_SINGLESTEP` command.
    *   Process stopped because some `ptrace` event was fired.
    *   Process stopped because it received a signal

As you can see, there are quite some cases to process, and we should deal with all of them in a real-world application. You’ll see soon that accounting for all these cases requires quite some code. It’s pretty straightforward code but it takes space, so I’ll show it now, and from this point on, I’ll keep using the simplified form in the code samples unless it’s really required to process some of those specific cases.

So, let’s assume our program is tracing some process with `ptrace` and is waiting for an event on it on a `wait`/`waitid` system call:

```C
int  status;
pid_t r = waitpid (pid, &status, __WALL);
// or
wait (&status);
```

The `__WALL` basically tells `waitpid` that we want to wait for all children, which includes other child processes as well as threads. `waitpid` accepts quite some options, which are all explained in the manpage, but I won’t go through all of them now as they won’t bring any useful information to our current discussion. Let’s first deal with the children dying.

```C
	if (WIFEXITED(status)) {
        printf("exited: %d\n", WEXITSTATUS(status));
        break;
    }

    if (WIFSIGNALED(status)) {
        printf("killed by signal: %d\n", WTERMSIG(status));
        break;
    }
```

The `status` value encodes quite some information in a single `int`; in order to access it in a portable way, we can use different macros, which will extract the relevant information from the integer value. In principle, the way this information is encoded is implementation-dependent, but for Linux, it usually follows the next encoding:

```
31                 16 15        8 7       0
+-------------------+------------+---------+
|   ptrace event    | exit code  |  state  |
+-------------------+------------+---------+
```

The higher 16 bits encode `ptrace` events. We’ll see those in a sec. Then come the process exit code, which takes 8 bits, and after that, the process state, which takes other 8 bits. Note that the exit code of a program, the parameter we pass to the `exit` system call, has to be 8 bits. Or, if you prefer, only the lower 8 bits of the value you pass will be considered. The `state` will encode the signal, which includes the die by signal as well as the `SIGTRAP` cases. In general, you don’t have to care about this, and you should use the `W*` macros we’ll see next, which actually deals with these details. So, let’s check those macros.

The code above makes use of `WIFEXITED` and `WIFSIGNALED` that will return nonzero if the program exited normally or exited because of a signal like `SIGKILL`. Additionally, the macros `WEXITSTATUS` extract from the `status` integer the exit value of the program (whatever is passed to the `exit` system call), and `WTERMSIG` will let us know which signal caused the process to die (`SIGKILL`, `SIGSEGV`, ...).

These two cases are important in case the process may end or crash due to our manipulation, and we may have control over that. The more interesting case for us is the one reported by `WIFSTOPPED`, which is nonzero when the process is in the stopped state. As discussed above, this may happen for different causes. Let’s see the general code to deal with this case and discuss again the possible cases:

```C
    if (WIFSTOPPED(status)) {
        int sig = WSTOPSIG(status);

        if (sig == (SIGTRAP | 0x80)) {
            // syscall stop
        } else if (sig == SIGTRAP) {
            // ptrace event / breakpoint / single-step
        } else {
            // signal-delivery stop
        }

        ptrace(PTRACE_CONT, r, 0, 0);
    }
```

Once we know that the process is in the stopped state, the first thing we should do is extract the information about which signal caused the process to stop. The macro `WSTOPSIG` will tell us. Now that we know the signal, we can process the different cases, which initially are just 2: either the signal is `SIGTRAP` or some other signal.

If it’s some other signal, `WSTOPSIG` has already told us what the signal the process received is, and that is many times enough. In case we need to get additional information about the signal, we can use the `ptrace` command `PTRACE_GETSIGINFO`, which is pretty useful if you use signals to exchange some information or you need further details of who and how the signal was generated. The code below shows how to use this command, and prints some of the returned info:

```C
      ptrace(PTRACE_GETSIGINFO, pid, 0, &si);
      printf ("Signal : %d\n", si.si_signo);
      printf ("Code   : %d\n", si.si_code);
```

The `code` field can be used to further determine the nature of the signal. The table below summarizes the main cases:

| Signal          | Code           | Description
| `SIGTRAP`       | `TRAP_BRKPT`   | Break point
| `SIGTRAP`       | `TRAP_TRACE`   | Single Step
| `SIGTRAP`       | `TRAP_HWBRKPT` | Hardware Break point
| `SIGXXX`        | `SI_USER`      | User sent signal
| `SIGXXX`        | `SI_TKILL`     | Signal sent from `tgkill`
| `SIGSEGV`       | `SEGV_MAPERR`  | Fault

We’ll discuss the `SIGTRAP` case next, but you can see from the table above that the `PTRACE_GETSIGINFO` command can also be used when processing `SIGTRAP` to extract further information about what caused the `SIGTRAP`.

I’m sure you noticed an extra case in the code to process the received signal: `SIGTRAP | 0x80`. What’s that? Well, there is a special case `ptrace` offers when tracing system calls. We can make our process stop before entering a syscall and just after leaving the syscall. Effectively, those cases also produce a `SIGTRAP`, however it’s useful to differentiate that `SIGTRAP` from the ones produced by a breakpoint, a step-by-step execution, or other `ptrace` event. For that, `ptrace` offers the `PTRACE_O_TRACESYSGOOD` option, which will set bit 7 of the signal value returned by `wait` when the `SIGTRAP` was produced by entering or leaving a syscall. You can activate this behavior adding the following line to your program:

```C
    ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
```

Why bit 7? Well, if you list the available signals in the system using `kill -l`, you’ll see that there are 64 of them. Meaning that we just need 6 bits to encode them, leaving the upper two bits unused. So, `ptrace` uses the higher bit of the signal information to encode this special case.

So, if we don’t enable this option, we’ll get `SIGTRAP` for all `ptrace` events, including entering and leaving syscalls, and we’ll have to figure out which one is the correct case. Note that `PTRACE_GETSIGINFO` won’t tell us if the stop was because of a system call or other reason. Enabling the option, we’ll get the 8th bit of the signal number set when the `SIGTRAP` was generated by a system call.

There is quite a lot to chew here right? Well, we're almost done, just two more topics to go.

The next point are the so-called `ptrace` events. You may have seen that in the comments in the code as well as in the `status` break-down. So, what are those `ptrace` events? They are special `SIGTRAP`s that will be sent on special situations. Those situations are controlled by the `PTRACE_SETOPTIONS` command. These are quite a few, so I’ll just mention here the main ones so you get an idea of which kind of events we’re talking about. Please refer to the man page for a full list.

*   `PTRACE_O_TRACECLONE`. When this option is enabled, a `PTRACE_EVENT_CLONE` event will be received when a tracee runs a `clone`. The tracer will start tracing the cloned process.
*   `PTRACE_O_TRACEFORK`. When this option is enabled, a `PTRACE_EVENT_FORK` event will be received when a tracee runs a `fork`. The tracer will start tracing the forked process.
*   `PTRACE_O_TRACEEXEC`. When this option is enabled, a `PTRACE_EVENT_EXEC` will be received when the tracee process runs `execve`.
*   `PTRACE_O_TRACEEXIT`. This option enables a `PTRACE_EVENT_EXIT` just before the process exits.

There are additional options and events for `vfork`, `seccomp`, and more. Check the man page for the whole list. The events are enabled using the constant above and the `PTRACE_SETOPTIONS` command, but how do we get them? The answer is in the `status` returned by `wait`, but the way to check them is a bit unusual. The piece of code below shows an example of processing some basic events:

```C
if (WIFSTOPPED(status)) {
    unsigned int event = (unsigned int)status >> 8;

    switch (event) {
    case SIGTRAP | (PTRACE_EVENT_FORK << 8):
        printf("fork event\n");
        break;

    case SIGTRAP | (PTRACE_EVENT_CLONE << 8):
        printf("clone event\n");
        break;

    case SIGTRAP | (PTRACE_EVENT_EXEC << 8):
        printf("exec event\n");
        break;

    case SIGTRAP | (PTRACE_EVENT_EXIT << 8):
        printf("exit event\n");
        break;
    }
}
```

Additionally, each one of those events carries additional information. For example, a `PTRACE_EVENT_FORK` will tell us about the pid of the new created process, and a `PTRACE_EVENT_EXIT` will tell us about the exit code of the process. This information is accessible using the `PTRACE_GETEVENTMSG` `ptrace` command. The returned information depends on the event, and once again, you can get all the details from the man page.

The final action we can do with the signals received by the process we’re tracing is manipulate them. We can do this using the last parameter of `ptrace` when using the `PTRACE_CONT` command that lets the traced program continue. Usually, we use zero for that parameter, which means no signal. If you check `kill -l` to list all available signals, you’ll see that there is no `0` signal. But if we pass a different value, we’ll inject that signal into the process. This allows us to capture and ignore signals (passing always zero to the process), let the regular signal pass (just reinjecting it in the process), or manipulate it into a different signal.

For example, `gdb` provides the `handle` command that allows us to do just this. We can tell `gdb` to stop the execution of the process (keep it in the STOPPED state), to print a message, or to pass the signal to the process to let it process it. For example:

```
(gdb) handle SIGUSR1 nostop noprint pass
    Signal        Stop      Print   Pass to program  Description
    SIGUSR1       No        No      Yes              User defined signal 1
```

This will tell `gdb` to not stop if a `SIGUSR1` is received, to not print anything, and to pass the signal to the program to let it process it.

Quite some information in this section. You can get back to it when you need it. As I said, I won’t use the complete signal handling process in the `ptrace` examples, but be aware that not managing the signals properly may lead to malfunctioning of the programs. Also note that there would be cases where many of the cases are irrelevant and can be safely ignored by our programs.






## Debugging with special debuggers

The same way that malware or games developers work hard to avoid debugging, researchers try equally hard to be able to debug those programs. We’ve just seen how to manually disable checks by patching or skipping parts of the program manually, but there are more sophisticated solutions used to fool most of the anti-debugging techniques we had seen so far.

Let’s talk about a few of them and implement some examples, but, as you can imagine, this is a cat and mouse game. Whenever the good guys manage to detect some techniques, the bad guys come with a new technique to avoid that, and then the good guys learn about that technique and can also workaround it, but then the bad guys… You know how it goes right?. That means that this chapter may potentially be endless, so we better stop here where we reach a reasonable level of complexity and enough understanding for you to go on your own and investigate more advanced techniques, offensive and defensive.

We’ll talk about two main techniques as most of the more advanced techniques may require us to write kernel modules or work with VMs and hypervisors, which is a bit out of the scope of this book. They are both fascinating topics to explore on your own, so I don’t want to ruin your fun ;) . Let’s learn about two techniques that researchers use to fool programs that try to fool debuggers.

### System Call Emulation

The `ptrace` system call has a command named `PTRACE_SYSCALL` which will produce a `SIGTRAP` just before entering a system call and after returning from it. I briefly mentioned it before. This command can be used to monitor system calls execution by the target in a similar way to what `strace` does. As `ptrace` is a system call itself, we can intercept any call to `ptrace` (the system call, not the libc wrapper) and simulate its execution to fool the anti-debugging technique to believe that there is no debugger attached.

In addition to `ptrace`, we can do that very same thing with other system calls. For example, we can also detect any call to `read` and check the returned buffer, looking for the string `TracerPID:`, and change our PID (that will be shown there as we’re the process tracing the target) to a 0 so that detection technique won’t work either. Same with the PPID field or the `getppid` system call.

As you can see, the initial debugger takes full control of the process and modifies the system calls the target performs so they return values that will prevent the detection. In other words, the debugger creates a fake virtual environment to make the program believe it’s not being executed inside a debugger. Something like living in the Matrix.

The following program captures all the target system calls and whenever it performs a `ptrace (PTRACE_TRACEME)`, it forces the `rax` register value into zero after returning from the syscall (we could skip the system call at all in this case), effectively disabling that check.

```C
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
    printf ("Entering syscall: %d\n", regs.orig_rax);
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
```

> **TECHNIQUE:** Trace `ptrace`/`ptrace` virtualization
>
> _ Debugging/Syscall Hijacking/ptrace emulation/ptrace virtualization_
>
> Using `ptrace` `PTRACE_SYSCALL` command to track system calls and hijack calls to the `ptrace` system call itself.

Note that when we enter the syscall, we have to check the `regs.orig_rax` register to figure out which system call is being executed, but when we leave, we have to look at the `regs.rax` to check the system call result. This technique works even if the target process calls `ptrace` directly from assembler.

>_NOTE: I’m assuming that the first call to `PTRACE_SYSCALL` will stop me in the entry to a system call, and that was the case in all my tests, but it’s better to double-check it. You can use `PTRACE_GET_SYSCALL_INFO` after a `PTRACE_SYSCALL`+`wait` to know if you’re entering or exiting a system call. In our case, as we've to read the registers after each stop, we can also check the value of `rax` which will be `-38` (`-ENOSYS`) when we’re about to enter a syscall. Note that there are edge cases where this last test may fail, so if you want to be fully sure, use `PTRACE_GET_SYSCALL_INFO`._

We can now extend this example to also monitor the `read` system call. This is going to be a bit longer, but conceptually it works in the exact same way. We’ll have to use our functions to read and write memory from the target process, though. This is the complete code:

```C
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

```

> **TECHNIQUE:** Trace arbitrary system calls
>
> _ Debugging/Syscall Hijacking/ptrace emulation/ptrace virtualization_
>
> Using `ptrace` `PTRACE_SYSCALL` command to track system calls and hijack arbitrary system calls, optionally writing memory on the target process to modify input or output buffers.

The main loop is identical to the previous case, but now we hook on syscall `0` (`read`). I’ve just adjusted this to work with our example so I assume that any `read` not from `stdin` is a `read` from `/proc/PID/status`. To make the program work in all cases, we need to also hook on the `open` or `openat` system calls and look for that path (`/proc/PID/status`). Note that, in order to get the path, we need to read memory from the target process because that string is stored in the target process memory. See how the `read` hook does it, and you’d have to do something similar.

> **EXERCISE**
>
> Update the program above to capture the relevant file descriptor (`/proc/PID/status`) and only patch `read` syscalls on the right file.

Then, on the `read` syscall exit (not for `stdin` or `rdi != 0`), we just read the buffer (pointed by `rsi`) from the target process, then we look for the `TracerPid` string and overwrite whatever is there with `"0\n"` and finally we write the buffer back in the target process. As you can see, it’s quite easy to manipulate the values read for a process using `ptrace`.

I left the trace lines to show the syscall execution (the syscall entering point). You’ll notice that, despite the original program doing an `fgets` for each line, libc just calls the `read` syscall once and then works using what is already buffered in memory from the first read. While it has lines to return it won’t read the disk again. You can print `rdi, rsi, rdx, ...` and show the parameters passed to the system call and also print `rax` on the exit to check the return values. For `read`, you’ll see that `rax` will return `1024`, which is the amount of data read from the file.

Regarding detection of this kind of technique, in principle, execution of the program should be slower because we’re stopping at each single syscall and doing some checks and tasks on specific ones; however, as we’ve discussed, the detection margin is small and hardware-dependent, so such a detection may be difficult. In this case, you have to come up with innovative ideas to achieve detection, usually based on knowledge of how the detection is performed. For example, for our test example above where we monitor the `read` system call, a program could `mmap` the file instead of `read` it and avoid the system call filter we've implemented in our program. Note that this is just an example, things are not that easy in the real world, and you’ll end up with more specific and sophisticated solutions.

Even when we’ve just seen a couple of examples, you have learnt the very basic foundations used by `strace` and also how you could write your own `strace` version with built-in anti-anti-debugging capabilities. That could be a nice project you might try. There are some optimizations we can do for this technique to filter the system calls we’re interested on and only run extra code when we really need, but for this first chapter, we can close this topic now, at least, for the time being.

<!--- TODO: ptrace virtualization with seccomp To be added on seccomp chapter --->

### Code instrumentation

This is one of the most used techniques, and the one implemented for example by Frida, Pin, or DynamoRIO, to mention just a few very well-known tools in the world of binary instrumentation. In this case, the solution is to avoid the use of `ptrace` at all (well, almost) and instrument the code—that is, to modify the program code so it will give control to our own code at interesting points in its execution. Oversimplifying the process, you basically inject a jump before or after the code block you’re interested in, in order to jump into a function that logs parameters, modifies those parameters, or changes the return values in case of returning from a function, for example.

The instrumentation process is not straightforward. There are two main approaches. The first one consist on modify the existing code injecting jumps to our own code (what we want to do when the function is executed). Similar to what we've been doing so far. Then, our own code, before returning the control has to restore the original code and, somehow arrange a new call for re-installing the jump in case that is required. The other approach is to make a copy of the original program with the injected instrumentation, effectively generating a new version of the program with all the instrumentation in place. These approach is pretty similar to a JIT compiler like the ones used by languages like Java or JS. This is for example the approach followed by tools like Frida. I bet you may be pretty confused by now. Don't worry we'll see how all these techniques work in the next chapter and then everything will make sense.

This kind of systems requires the injection of some code in the target process usually known as an agent that will accept our commands and perform the operations we asked or the first code injection to get the instrumentation process start up. In other words, we're executing a debugger but without using `ptrace`. 

In any case, what we have seen so far is somehow the first piece we'll need in order to understand how these tools work

<!---

TODO: Add examples of trampolines, break points and thunk functions
### Breakpoints, Trampolines and Thunk functions
--->

