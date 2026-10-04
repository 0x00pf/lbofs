# Debugging and Anti-debugging Fun with `ptrace`

Let's start with one of the most powerful system calls provided by the Linux kernel: `ptrace`. In this chapter, you'll learn how to use this system call to gain full control of any process in your system. You'll also learn how some malware use it to avoid being debugged, modify program flow, or inject code into any process in the system. Yes, you can do a lot with this single system call.

We'll learn about the traditional anti-debugging techniques used by legitimate and not-so-legitimate programs and find out how many of them are closely related to the `ptrace` system call. Then, we'll learn how to control registers and memory of any process. These are classical debugger tasks, but as we'll see, they can be used for purposes beyond finding and fixing bugs—in other words, we'll learn how to use a debugger as an offensive tool.The chapter will end with a final project where we'll implement our own version of _nanomites_, an old but effective anti-debugging technique that makes extensive use of `ptrace`. We'll implement a simpler version, so we'll call them _picomites_, but at the end, you'll have a pretty good understanding of how this technique works. So, let's get ready—it's going to be an intense and hopefully interesting chapter.

Before going into the details, let me warn you that we'll come back to `ptrace` in later chapters. `ptrace` is one of those system calls that, when combined with others, can be used to implement very advanced techniques, so we'll keep coming back to it as we introduce more system calls in next chapters.

## Meet `ptrace`

Initially intended for writing debuggers (still its main purpose), `ptrace` can be abused in many different ways to implement elaborate techniques for code injection, data extraction, implement anti-debugging techniques, or run malicious code from inside trusted applications in the system. In general, `ptrace` allows a program, with the right permissions, to take over any other process in the system, look into its memory, manipulate its registers, and even rewrite parts of it. Said that, you can imagine why this is something interesting for many malware writers or people who write protection and anti-copy systems, and we'll see how to do all that in this chapter. But first, let's learn a little bit about this system call.

`ptrace` is one of those system calls that accepts commands (like `ioctl` or `fcntl`) and, therefore, expects to receive different types of arguments, depending on the command you want to use. Its prototype is shown below:

```C
#include <sys/ptrace.h>

long ptrace(enum __ptrace_request op, pid_t pid,
            void *addr, void *data);
```

The first parameter the system call takes is the command you want to run. There are quite a few, and we'll go through the main ones in this chapter. We'll see some more in other chapters, and for those, feel free to check the man page (`man ptrace`). There's really a lot of information in this man page, and it's worth reading it all. Then it follows the `PID` or process identifier (if you prefer) to whom you want to run the command against. Finally, there are two generic parameters representing a memory address and a value. Depending on the first parameter, the `ptrace` command we want to run, one or the other (or both) will make sense. In general, if a command is intended to interact with a memory address, the `addr` parameter should be set, and if the command expects to receive or write any value, the `data` parameter will be used. As I said before, you can find all the details in the `ptrace` man page for the different commands.

Whenever any of the parameters are not required, they can be set to 0. Later `libC` versions define the `ptrace` prototype as a variadic function, so you can just omit null parameters at the end and call the wrapper just with the parameters you need, but I prefer to always pass the four parameters to each call. That's up to you.

## Tracing Programs and Anti-debugging

The main purpose of `ptrace` is to trace/debug programs, and because of that, it's closely related to many anti-debugging techniques. In order to understand those anti-debugging techniques, we first need to know how to use `ptrace` for debugging, and the first step is to know how to start debugging a process. We can do that in two different ways: creating a child process and make it ask to be traced, or attaching/seizing a running process (either our own child or not). We'll see how to do both, but let's start with the first case, tracing our own children, because that's conceptually simpler and will directly introduce one of the most classical anti-debugging techniques.

The typical code to start tracing a process is shown below. We'll see this code again towards the end of the chapter while working in our final project:

```C
pid_t pid;
if ((pid = fork ()) < 0) exit (1); // ERROR
else if (pid == 0) { //Child Process
        ptrace (PTRACE_TRACEME, 0, 0, 0);
        execl ("Some program", NULL);
} else { // Parent Process
        int status;
        wait (&status);
}
```

Once the main program (the tracer) is forked, the child process will immediately call `PTRACE_TRACEME` before `execing` the new program, which is usually the one we really want to trace (the `exec`ed program, not the child itself). This is what `gdb` does when you pass a program to it as a parameter and press `r` to start running it.

Calling `ptrace` with the command `PTRACE_TRACEME` will tell the kernel that this process wants to be traced, but it returns immediately without any further effect. After the call to `exec`, the child process (because it indicated that it wanted to be traced) will stop and send a `SIGTRAP` signal to the parent process, which is usually waiting for the child in a `wait` system call. The `wait` system call is the way `ptrace` uses to communicate the tracer/debugger with the tracee/target process. We’ll see this a hundred times along this chapter, and it’ll become second nature to you by the end of it.

At that point, after the parent returns from the `wait`, the parent process (usually a debugger) takes control of the child process and can do further `ptrace` operations on it, like read its memory or run parts of the program. We’ll see this in detail along the chapter, but many operations require the child to be in the stopped state. This may be a bit confusing at first, but it’ll make perfect sense as we progress. For example, if you want to read the registers of a process, you want that process to be stopped in a steady state; otherwise, we could get wrong values in registers, or half of the registers with the values of the previous instruction and half with the current instruction. I hope you see the point. We need to stop the target process before manipulating it. Even if you ignore all those good reasons and go ahead, the kernel will just stop you with a, sometimes, cryptic error whenever the `ptrace` operation requires the process to be stopped and it isn't.

Alternatively, the parent process can just attach to the just-created child process using the `PTRACE_ATTACH` command, which we’ll discuss later in this chapter. Both approaches produce the same result: the child process being traced and in the stopped state, ready to be manipulated by further `ptrace` calls. This last operation is equivalent to running `gdb` with the `--pid` flag indicating the PID of the process we want the debugger to attach to. We’ll see how this works later in the chapter.

Coincidentally, using `PTRACE_TRACEME` happens to be one of the simplest and oldest anti-debugging techniques, just **because any process in the system can only be traced by one process at a time**. In other words, you cannot trace a process that is already being traced. This fact was abused by programs trying to prevent being debugged by just calling `ptrace` with the `PTRACE_TRACEME` operation, which will fail if the process is already being traced. Then, the program can decide to refuse to work when it’s loaded in a debugger, which is the usual case. This is maybe the easiest way to detect if a program is being executed inside a debugger or not. The following code shows an example of how to use this technique.

```C
#include <stdio.h>
#include <sys/ptrace.h>

int main () {
  if (ptrace (PTRACE_TRACEME,0,0,0) < 0) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Program runs normally\n");
  return 0;
	    
}
```

> **TECHNIQUE**: Debugger detection with `PTRACE_TRACEME`
>
> _Anti-debug/Debugger Detection_
>
> Call `ptrace` with command `PTRACE_TRACEME` to determine if a debugger is already tracing us


You can compile the program and run it:

    $ make test1
	$ ./test1
	Program runs normally
	$ gdb -q ./test1
    Reading symbols from ./test1...
    (gdb) r
    Starting program: /opt/syscalls/ptrace/test1
    [Thread debugging using libthread_db enabled]
    Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
    Press a Key

    Debugger detected. Aborting
    [Inferior 1 (process 3084535) exited with code 0377]
    (gdb)

When running with `gdb`, the program will detect the debugger and just abort the execution. Unfortunately, this naive way of using `PTRACE_TRACEME` is very easy to break just by writing a small shared library that intercepts the calls to `ptrace`.

### Using `LD_PRELOAD` to Disable Anti-debug Functions

Actually, this is a general technique that can be used to disable or, in general, modify the behavior of any function provided by a dynamic library.

`LD_PRELOAD` can be used to instrument your code for purposes like debugging, auditing, profiling, or adding functionality like, for example, support for connection through a proxy, just to mention some of the traditional uses. It’s also the main technique used to implement user-space rootkits because it allows you to easily inject code in monitoring tools to hide other programs, connections, or files. `LD_PRELOAD` is just an environmental variable used by the dynamic linker.

When a dynamically linked program is executed, the associated dynamic linker—the one indicated in the `interp` section of the file—is in charge of loading the program itself and any required library. The dynamic linker keeps a list of the required libraries and, whenever a symbol needs to be resolved, will go and look for it through all those libraries (well, when `LAZY_BINDING` is used, which is the default behavior). The `LD_PRELOAD` environmental variable allows us to put some libraries first in line for resolving symbols, telling the dynamic linker: _Hey guy, look! Whenever you need to resolve a symbol, look first in this libraries_. This way, we can inject code in any dynamic process and make sure it gets executed before any other.

> **ROOTKITS**
>
> A rootkit is a program or set of programs intended to hide something in the system. The first rootkits consisted of modified versions of the main monitoring tools like `ps`, `netstat`, `ls`, etc. Those programs were modified so they wouldn’t show processes, network connections, or files selected by the attacker, making malware invisible in the system. As mentioned, originally, a rootkit was composed of quite a few programs, and its main goal was to keep something hidden, usually some program to keep root access to the machine, and that's where its name comes from.
>
> Rootkits evolved quickly and soon they moved into the kernel as sophisticated kernel modules able to hide elements at the kernel level, so there’s no need to modify a whole bunch of tools. Changing some system calls will hide something to all user-space tools in one go. For example, if the rootkit wants to hide some file (the original malware, for example), it could patch the `getdents` system call so that file is never reported to any tool scanning folders to enumerate files, effectively making that file invisible. Usually, a rootkit had to modify a few system calls to keep processes, files, and network connections hidden.
>
> Kernel rootkits have the additional advantage that it doesn't matter which user-space tools are used. The original rootkit worked fine in older systems where monitoring was performed using the standard tools available in the system. As new tools started to be developed, then, a given system may use one tool and another a different one, and keeping your *kit* up to date started to be an issue as it should include a lot patched tools to ensure it’ll work on any system.
>
> The original rootkits we’ve been talking about worked in user-space. But because of this last problem I mentioned (having multiple tools for monitoring), user-space rootkits evolved towards patching the system libraries instead of the tools themselves. Patching the libC `opendir/scandir` functions will likely affect most tools listing files. And this is when the dynamic linker hacks came into play. `LD_PRELOAD` is one of the simplest ways to get code injected in any program using a given function. Changing the `/etc/ld.so.conf` file will also alter the way and list of libraries to be used, and the most advanced user-space rootkits provided modified versions of the dynamic linker, which give you full control on how the program is loaded and external symbols resolved.

For our current anti-debug solution using `ptrace`, the following code will disable the `PTRACE_TRACEME` operation on the libC `ptrace` wrapper.

```C
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <sys/ptrace.h>
#include <dlfcn.h>
#include <sys/types.h>

long (*orig_ptrace)(enum __ptrace_request, pid_t, void *, void *) = NULL;

// Match the exact variadic signature of the system's ptrace
long ptrace(enum __ptrace_request request, ...) {
    va_list ap;
    va_start(ap, request);
    
    // Extract the standard arguments from the argument pool
    pid_t pid = va_arg(ap, pid_t);
    void *addr = va_arg(ap, void *);
    void *data = va_arg(ap, void *);
    
    va_end(ap);
    if (request == PTRACE_TRACEME) {
        fprintf(stderr, "[LD_PRELOAD] Intercepted ptrace(PTRACE_TRACEME). Returning 0.\n");
        return 0; 
    }

    if (!orig_ptrace) orig_ptrace = dlsym(RTLD_NEXT, "ptrace");
    
    return orig_ptrace(request, pid, addr, data);
}
```

> **TECHNIQUE**: Function injection with `LD_PRELOAD`
> _Function Hijacking/Code Injection_
>
> Use `LD_PRELOAD` to substitute an existing version with your own version. Only works on dynamic binaries


We need to define a function with exactly the same name and prototype as the function we want to hijack, so the variadic version of the libC wrapper. Then, within that function, we can use `dlsym(RTL_NEXT, "function")` to obtain the pointer to the next (`RTL_NEXT`) function implementation available. This tells the dynamic linker: _Look for this function in the rest of the libraries in your list_. Note that this will return just the next one, but the same function can be overwritten many times. Then, in our interception function, we just do whatever we want, like ignoring some parameters, returning certain values depending on the function parameters, or just calling the normal function to actually perform the task the function was intended to do.

In this example, we just check if the first parameter is `PTRACE_TRACEME` and return 0 to the calling function no matter what, so the program will believe it’s not being traced even when it really is. For any other `ptrace` call, we just call the original `ptrace` function normally. We can compile this into a library with the following command:

```bash
$ gcc --shared -fpic -o libbypass_ptrace.so bypass_ptrace.c
```

And then set the `LD_PRELOAD` environment variable to force the pre-loading of our library before/while running the program with `strace` and see how the anti-debug check is ignored.

```bash
$ strace env LD_PRELOAD=./libbypass_ptrace.so ./anti_debug
```

Or we can do the same in gdb:

```bash
$ gdb -ex "set environment LD_PRELOAD=./libbypass_ptrace.so" ./anti_debug
```

Note the fancy syntax. This is required to set the `LD_PRELOAD` variable only for the program they are going to trace and not for themselves (`strace` or `gdb`) which may prevent them to work properly.

This is a pretty straightforward way to disable this anti-debug technique, however, it’s also very easy to circumvent. The program just needs to call the syscall directly, instead of using the libC wrapper. In that case, there is no function involved, so nobody can hook any library on it using `LD_PRELOAD`.

In those cases, the researcher needs to find the part of the program where the debugger is detected and either remove it (for instance, writing `nop`s where the check is done) or skip it during debug (forcing the program counter to the instruction after the check, for example), either manually or using a debugger script, which is very useful in these kinds of situations.

This is a version of the previous program calling the `ptrace(PTRACE_TRACEME)` syscall directly from ASM using inline ASM.

```C
#include <stdio.h>

int main () {
  int r = 0;
  
  __asm__ volatile (
		    "movq $101, %%rax;"  // Load the ptrace syscall number
		    "movq $0, %%rdi;"    // Request: PTRACE_TRACEME (0)
		    "movq $0, %%rsi;"    // PID: 0
		    "movq $0, %%rdx;"    // Addr: 0
		    "movq $0, %%r10;"    // Data: 0
		    "syscall;"           // Syscall
		    : "=a" (r)           // Output: value of rax goes into the C variable 'r'
		    :                    // No input variables mapped inside the asm template
		    : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "memory" // Clobber list
		    );
  
  if (r) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");
  return 0;	    
}
```

> **TECHNIQUE:** Direct system call invocation (`PTRACE_TRACEME`)
> _Antidebug_
>
> Use direct system call invocation to avoid function hijacking using `LD_PRELOAD` technique



In those cases, the researcher needs to find the part of the program where the debugger is detected and either remove it (for instance, writing `nop`s where the check is done) or skip it during debug (forcing the program counter to the instruction after the check, for example), either manually or using a debugger script, which is very useful in these kinds of situations.

This is a regular syscall in inline asm. `rax` will contain the system call, which, for `ptrace`, is 101. Then, `rdi`, the first parameter, shall contain the operation, `PTRACE_TRACEME`, whose value is zero. All other parameters shall be set to zero. Actually, `rdx` and `r10` are ignored by the syscall when `PTRACE_TRACEME` is used, so we could just avoid the initialization, but let’s write the code for the complete `ptrace` syscall so you can reuse it with commands that require all four parameters.

> **gcc inline asm**
>
> `gcc` allows us to inject ASM code directly into our C program using `__asm__`, which has a somewhat unintuitive syntax. Basically, you write the ASM as a string that will be passed to the assembler directly, including the assembly code you want to generate. Then follows three colons. The first one allows us to map registers to output variables for the ASM code. In the example above, `"=a" (r)` means map register `RAX` (that’s the `=a`) to the C variable `r`. Then, after the second colon, we can define any parameter we want to feed into the ASM in a similar way. In this case, there is no input parameter, so we leave that part empty. Finally, the last colon allows us to indicate which registers are clobbered by the code.
>
> What’s that clobbering about? Well, what happens is that the ASM code will be passed directly to the assembler so the compiler has no clue of what it does. For example, which registers it modifies. This last part of the command allows us to tell the compiler: Hey look!, this code here will change all those registers, please take this into account for the code you’re going to generate around this ASM block or any optimization you’re planning to do. If you’re curious on how to use `__asm__`, there are pretty good online resources explaining the syntax. It takes a while to get used to it, but it’s indeed a very useful skill to have under your sleeve.
>
> Note that you can call a syscall directly using the libC `syscall` wrapper, but in that case, you’re calling a function, and therefore the `LD_PRELOAD` trick can be used on the `syscall` wrapper itself.

If you now try to use the `LD_PRELOAD` trick, it won’t work. The solution to disable the anti-debug check is now to find a suitable point in the binary code to patch the program so the check never happens. In other words, the researcher has to disable the check or remove the checking code, whatever terminology you prefer. This can be done in many different ways.

### Patching Binaries to Disable Anti-Debug Code

Let’s take a look at the ASM/machine code generated for our previous example:

```bash
$ objdump -d test2 | grep -A10 "<main>:"
0000000000001139 <main>:
    1139:        55                           push   %rbp
    113a:        48 89 e5                     mov    %rsp,%rbp
    113d:        48 83 ec 10                  sub    $0x10,%rsp
    1141:        c7 45 fc 00 00 00 00         movl   $0x0,-0x4(%rbp)
    1148:        48 c7 c0 65 00 00 00         mov    $0x65,%rax
    114f:        48 c7 c7 00 00 00 00         mov    $0x0,%rdi
    1156:        48 c7 c6 00 00 00 00         mov    $0x0,%rsi
    115d:        0f 05                        syscall
    115f:        89 45 fc                     mov    %eax,-0x4(%rbp)
    1162:        83 7d fc 00                  cmpl   $0x0,-0x4(%rbp)
    1166:        74 16                        je     117e <main+0x45>
    1168:        48 8d 05 95 0e 00 00         lea    0xe95(%rip),%rax        # 2004 <_IO_stdin_used+0x4>
```

The analyst has many options to defuse the anti-debug check. For example, setting the byte at `0x114b` to zero, which effectively will change `movl $0x65,%rax` into `movl $0,%rax` followed by nopping the `syscall` instruction. That’s writing `0x90` in position `0x115d` and `0x115e`. You may be wondering why patch `0x114b`, well, in order to pass the check, that is actually performed at `0x115f` `rax` has to be zero, and that is the best place to do that patching as it needs just one byte.

> **NOPPING**

> All processors provide a `NOP` or similar instruction. `NOP` stands for `No Operation` and basically means, do nothing. Despite how convenient this is for our current applications—we’re using it to remove code that’s bothering us while reversing a given program—you may be wondering why an instruction that does nothing is included in a processor. Actually, there are a few very good reasons for that:
>
> *   Patching. This is our current application. We’re using it to remove code that’s bothering us while reversing a given program; however, during development, being able to disable parts of the code during debugging is pretty useful.
> *   Alignment. `NOP` instructions can be used to align code, for example, to make jumps target addresses that are multiples of the native word size and therefore will be more efficiently taken.
> *   Timing. This is more rare nowadays, but inserting `NOP`s was a well-known technique used in old systems for precise timing or delay when writing assembly code. The `NOP` instruction, even when it does nothing, requires some CPU cycles to run. Adding a specific number of `NOP` instructions allowed programmers to control very small and accurate delays in the code.
>
> There are more obscure uses of `NOP` instructions, like hardware testing (wire the data bus to a `NOP` opcode, actually switches, and make the processor run through all the memory addresses) or pipeline cleanup on certain exotic processors. Maybe the use that you may be more interested in, as well as patching, are *NOP sledges*. Old buffer overflow techniques had to perform a jump back into the stack where they inserted the code. However, it’s impossible to precisely predict the exact address the stack pointer will be pointing to, so exploit developers used this technique, which consists of adding a long sequence of `NOP`s and then performing an *approximate* jump back. That jump will end up in the *NOP sledge*, which will let the instruction pointer smoothly slide into the injected code.
>
> Almost any platform has a `NOP` operation or an equivalent operation that can be used instead, but the machine code associated with each processor is very different.
>
> Architecture        |Mnemonic |Hardware Mnemonic      |Machine Code (Hex)
> ----------------    |-------- |-----------------      |---------------------
>     Intel           |NOP      |NOP                    |0x90
>                     |ENDBR64  |ENDBR64                |0xf3 0x0F 0x1E 0xFA
>             2 bytes |NOP r/m  |data16 nop             |0x66 0x90
>             3 bytes |         |nop DWORD PTR [eax]    |0x0F 0x1F 0x00
>     		  4 bytes |         |nop DWORD PTR [eax+0]  |0x0F 0x1F 0x40 0x00
>     		  5 bytes |         |nop DWORD PTR          |0x0F 0x1F 0x44 0x00 0x00
> 			          |         | [eax + eax*1 + 0x0]   |
>     		  6 bytes |         |nop DWORD PTR          |0x66 0x0F 0x1F 0x44 0x00 0x00
> 			          |         |[eax+eax*1+0x00000000] |
>     		  7 bytes |         |                       |0x0F 0x1F 0x84 0x00 0x00 0x00 0x00
>     		  8 bytes |         |                       |0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00
>     		  9 bytes |         |                       |0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00		
>     ARM32 (A32)     |NOP      |NOP                    |0xE320F000 
>                     |         |MOV R0, R0             |0xE1A00000
>           Thumb     |NOP      |NOP                    |0xBF00
>           Thumb     |         |MOV R8, R8             |0x46C0
>     	    Thumb2    |NOP.W    |                       |0xF3AF 0x8000
>     AArch64 (ARM64) |NOP      |NOP                    |0xD503201F4
>     MIPS (32/64-bit)|NOP      |SLL $0, $0, 0          |0x000000004 
>     RISC-V 64-bit   |NOP      |ADDI x0, x0, 0         |0x000000134
>      (C-Extension)  |NOP      |C.ADDI x0,0            |0x0001
>
> Yes, Intel processors offer multibyte versions of `NOP` to cover up to 9 bytes intended to force instruction alignment. Remember Intel processors have a variable-length instruction encoding, and all kinds of situations may happen regarding alignment. However, except for the compiler, you shouldn’t care about those... Just add as many `0x90`s as you need. I just added these cases to the table for completeness.
>
>Finally, note that for RISC processors, sometimes you have to use pseudo instructions, and if the platform has a zero register, it likely will use that to implement `NOP`.

Alternatively, the initial `mov $0x65, %rax` could be changed into a `jmp 0x117e`, which will skip the complete syscall and the comparison of the return value in one shot, and land us in the code branch we want, the normal program execution. Which option is better? It depends. I’d say the first one is safer as the program will execute as intended and will simulate a syscall returning 0. If `rdi` or `rsi` are assumed to be zero later in the program, skipping that initialization could lead to issues during the execution of the rest of the program.

Let’s look now at option 2, for which we can use a relative 8-bit jump, opcode `0xEB`. The offset needs to be calculated from the current `RIP` value at the moment of executing the instruction, to the target instruction. In this case, the instruction is at `0x1148`. If we change that `mov` to a `jmp`, that is 2 bytes long, `RIP` just before executing that instruction points to `0x1148 + 2`, so the offset we need is: `0x117e - (0x1148 + 2) = 0x34`.

Finally, even better, we can change `je` (opcode `0x74`) to `jmp` (opcode `0xeb`) and we could disable the debug check with just one byte patch. You can try all options and see how they work. Listing below summarizes all the options discussed.

```asm
1139:	55                   	push   %rbp
113a:	48 89 e5             	mov    %rsp,%rbp
113d:	48 83 ec 10          	sub    $0x10,%rsp
1141:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
1148:	48 c7 c0 65 00 00 00 	mov    $0x65,%rax        <-- OPT1: 0x114b = 0
                                                         <-- OPT2: 0x1148 = 0xEB 034
114f:	48 c7 c7 00 00 00 00 	mov    $0x0,%rdi
1156:	48 c7 c6 00 00 00 00 	mov    $0x0,%rsi
115d:	0f 05                	syscall                  <-- OPT1: 0x115d = 0x90 0x90
115f:	89 45 fc             	mov    %eax,-0x4(%rbp)
1162:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
1166:	74 16                	je     117e <main+0x45>  <-- OPT3: 0x1166 = 0xeb
1168:	48 8d 05 95 0e 00 00 	lea    0xe95(%rip),%rax  <== Debugger detected
116f:	48 89 c7             	mov    %rax,%rdi
1172:	e8 b9 fe ff ff       	call   1030 <puts@plt>
1177:	b8 ff ff ff ff       	mov    $0xffffffff,%eax
117c:	eb 14                	jmp    1192 <main+0x59>
117e:	48 8d 05 9b 0e 00 00 	lea    0xe9b(%rip),%rax  <== Normal Execution
1185:	48 89 c7             	mov    %rax,%rdi
1188:	e8 a3 fe ff ff       	call   1030 <puts@plt>

```
Note that the first case needs to patch 3 bytes, while the second case will have to patch 2 bytes, assuming we’ll use a near relative jump with an 8-bit displacement, which is OK for this case, and the third case just patches 1 byte. The number of bytes to patch isn’t really a metric for anything, but the less you touch the binary, the better.

You can patch the program using different tools. Hexadecimal editors, `vim` and `xxd`, `dd`, or `gdb`. Using `dd` is my favorite way, but all other are perfectly fine, and you may need them sometimes, depending on what’s available in the system you’re working on. Of course, more powerful tools like `Ghidra`, `radare2`, or `Binary Ninja`, just to mention a few, also allow you to patch the binary very easily from a comfortable graphical user interface, because patching is a very common operation, part of the regular workflow of analyzing binaries.

For all basic tools I mentioned above, except for `gdb` (which makes the process pretty trivial, similar to the more powerful tools), you need to figure out the offset of the bytes you want to patch inside the file. And for doing that, you’ve to look at the ELF program headers.

Let’s do the whole process from scratch, starting by looking into the program headers of our sample:

    $ readelf -l to_patch

    Elf file type is DYN (Position-Independent Executable file)
    Entry point 0x1050
    There are 14 program headers, starting at offset 64

    Program Headers:
      Type           Offset             VirtAddr           PhysAddr
                     FileSiz            MemSiz              Flags  Align
      PHDR           0x0000000000000040 0x0000000000000040 0x0000000000000040
                     0x0000000000000310 0x0000000000000310  R      0x8
      INTERP         0x0000000000000394 0x0000000000000394 0x0000000000000394
                     0x000000000000001c 0x000000000000001c  R      0x1
          [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
      LOAD           0x0000000000000000 0x0000000000000000 0x0000000000000000
                     0x0000000000000628 0x0000000000000628  R      0x1000
      LOAD           0x0000000000001000 0x0000000000001000 0x0000000000001000
                     0x000000000000019d 0x000000000000019d  R E    0x1000
      LOAD           0x0000000000002000 0x0000000000002000 0x0000000000002000
                     0x0000000000000134 0x0000000000000134  R      0x1000
      LOAD           0x0000000000002dd0 0x0000000000003dd0 0x0000000000003dd0
                     0x0000000000000248 0x0000000000000250  RW     0x1000
      __--snip--__

You need to pay attention to the `LOAD` program headers, which are the ones stored in the file and loaded into memory. Each `LOAD` segment indicates which part of the file, defined by the `Offset` and `FileSize` fields, will be loaded into memory at `VirtAddr`. This information allows us to map bytes in the file with bytes into memory or, alternatively, with the output of tools like `gdb` or `objdump`, as we’re doing in this case.

For our example, according to the `objdump` output we saw before, we have to patch addresses `0x114b`, `0x115d`, and `0x115e`, which are offsets in memory. Remember this binary is PIE. All those addresses are in the code segment (the second LOAD segment, the one with execution permissions), which is, conveniently, mapped to the same offset in the file. This is common with PIE binaries, but in other cases, you may have to check these values and calculate the correct file offset. So, in this case, the memory address reported by `objdump` and the offset in the file are just the same, which makes our lives much easier.

Do you still wonder why? Well, the code program header starts at offset `0x1000`, which is the first byte in the file that will be loaded into memory, also at memory offset `0x1000` (check the Virtual Address field); that’s enough to see that both outputs match. You can double-check with the output of `objdump`:

    $ objdump -d test2

    test2:     file format elf64-x86-64


    Disassembly of section .init:

    0000000000001000 <_init>:
        1000:	48 83 ec 08          	sub    $0x8,%rsp
        1004:	48 8b 05 c5 2f 00 00 	mov    0x2fc5(%rip),%rax        # 3fd0 <__gmon_start__@Base>
        100b:	48 85 c0             	test   %rax,%rax
    --snip--

So you can see that code starts at `0x1000` and all addresses in the `objdump` output are based on that address. You can also dump the file with `xxd` and check the bytes at offset `0x1000`; you’ll see `48 83 ec 08...`.

Let’s use an additional artificial example with more weird numbers so you can see how all this works in the more general case. Imagine that we need to patch address `0x1217`. That is the PIE offset shown by `objdump`. Now, imagine that you look at the segment and you see that the code segment (the one with `R E` permissions) is mapped at `0x1100` and that the disk offset is `0x3000`. In this case, we first need to calculate the offset of our instruction `0x1217` with respect to the memory address it’ll be loaded at `0x1100`; in this case, it’s `0x117`. Then, we know that the beginning of the code (`0x1100` in memory) is at offset `0x3000` in the disk. Therefore, address `0x1217` is `0x117` bytes away from the base address and also `0x117` bytes away from the base disk offset. In other words, the byte we’re interested in is located at offset `0x3117` in the file.

As you can see, the calculations are almost trivial. Just don't assume that memory offset and disk offset match, even when they do most of the time.

Non-PIE binaries usually have a different offset and memory address, but they tend to be *aligned* in the sense that the offset matches even when the absolute values are completely unrelated. For example, code at address `0x401234` will likely be stored at offset `0x1234` in the file, but you better check the program headers to be sure about the offset you have to use as, in theory, those values could point anywhere.

> **Patching Binaries**
>
> There are many different ways to patch a binary file using different tools. These are some of the most common ones:
>
> If you have an hexadecimal editor (ht, dhex, ghex, hexcurses, hexedit,...), just look for those offsets and enter a 0 and two 90s. You usually wouldn’t find these programs installed by default and you may have to install it yourself. Just try them all and choose the one you prefer. `ht` is pretty standard.
>
> If you have `dd` (which is usually available in all systems), you can use a command line like this:
>
> ```bash
> $ echo -n -e "\\x0" | dd of=to_patch seek=$((0x114b)) count=1 bs=1 conv=notrunc
> $ echo -n -e "\\x90\\x90" | dd of=to_patch seek=$((0x115d)) count=2 bs=1 conv=notrunc
> ```
>
> If you have `xxd` and `vim`, do the following:
>
> * Open the binary in `vim`. You will see a lot of garbage.
> * Now run the buffer through `xxd` using the command: `:%!xxd`.
> * You’ll now see the standard hexdump produced by `xxd` in `vim`.
> * Now look for the offset and edit the file entering the values you want to change. This will feel like using a hex editor.
> * Then you run the command `:%!xxd -r` to get your changes converted back to binary.
> * Now just save it and you are done using `:wq`.
>
> If you have `gdb`, use the following commands:
>
> (gdb) set write on
> (gdb) file ./to_patch
> (gdb) set {unsigned char)0x114b = 0x00
> (gdb) set {unsigned short)0x115d = 0x9090
> (gdb) disassemble 0x1139
> (gdb) quit
> ```
>
> The `disassemble` command is just to check that you’ve patched the right bytes.
>
> In chapter 4, we’ll see how to patch binaries from our own programs.

Patching binaries is a convenient way of progressively defuse protections in a binary, however the binary can easily detect that. We’ll see in a sec how, but first we need to talk about breakpoints, as those can be detected using the same techniques.

### Abusing Breakpoints

Breakpoints are one of the main elements in a debugger and therefore closely related to the `ptrace` system call, so I think this is a good time to introduce them because those key elements for debugging are also used in some anti-debug techniques as well as other techniques we’ll explore later in the book. To understand how breakpoints work, let me start by explaining how classical software breakpoints work on x86 processors. Actually, they really work the same way in all other platforms but just using different instructions.

Intel processors have a special software interrupt intended to stop a program for debugging purposes. Actually, they have two interrupts, but one specifically intended for breakpoints. For that reason, this instruction is only 1 byte long, so it can be placed anywhere in memory. The instruction in question is `int 3` with opcode `0xcc`. Whenever the processor hits this instruction, a `SIGTRAP` is generated. Well, actually, the processor produces an interrupt that is captured by the Linux kernel, which generates a `SIGTRAP` signal that is sent to the tracee program or to the installed signal handler.

Debuggers traditionally implemented software breakpoints this way. When you put a breakpoint at some specific address in memory, the debugger stores the byte in that position in some intermediate memory and substitutes it with `0xcc` so the process will be interrupted when reaching that point and the debugger will get control back. Note that nowadays processors also offer hardware breakpoints that work in a different way (usually by writing the address to break in a special register), however the amount of those HW breakpoints is limited, normally in the range of 2 to 8 depending on the platform, so you’ll end up using software breakpoints whenever your project requires multiple breakpoints.

Maybe you’ve already figured out how this technique works, but if not, no worries, just keep reading. The program trying to protect against debuggers (actually trying to detect them) will insert one of these instructions in its code and install a signal handler for `SIGTRAP`. The logic is as follows:

* If the program is executed inside a debugger, the debugger may get the `SIGTRAP` when the breakpoint instruction gets hit.
* Otherwise, the installed signal handler will be fired, and the program knows that it’s not running inside a debugger.

However, note that the debugger can feed the signal back into the process with `PTRACE_CONT` (we’ll see how this works soon). For example, that’s what `strace` does by default. So, this isn’t a reliable way to detect a debugger, but a way to know how it manages the `SIGTRAP` signals, and the basis for more sophisticated techniques that we’ll introduce towards the end of the chapter, so it’s beneficial to know how this works.

A simple implementation of this idea can be seen in the following code:

```C
#include <stdio.h>
#include <signal.h>

int debugged = 1;

void handler(int sig) {
  debugged = 0;
}

int main () {
  signal (SIGTRAP, handler); 
  __asm__ volatile ("int3");
  
  if (debugged) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE: Force software breakpoint to detect debugger**
>
> _Antidebugging_
>
> Install a signal handler for `SIGTRAP` and force a software breakpoint to verify if we can receive the trap.


Note that gcc’s inline assembly expects AT&T syntax, so we have to use `int3` or `int $3` and not `int 3`. For using this last one, we’ve to switch to Intel syntax.

> **Switching to Intel Syntax**
>
> As I said before, whatever is passed to `__asm__` will be forwarded, as is, to the assembler. That means that you can add `gas` (GNU Assembler) directives in your inline assembler, as for instance, `.intel_syntax noprefix` and start using Intel syntax in your in-lined assembly code. If you do that, don’t forget to restore the AT&T syntax before leaving your in-lined assembler, otherwise the rest of your program will just fail compiling.
>
> `gcc` converts the C code into ASM that it passes to gas afterwards. The inline assembly we use is just blindly added to the generated ASM code that will be passed to gas, that means that gcc will be generating code using AT&T syntax for your C program. Suddenly it finds your inlined assembly and copies over that text to the output ASM and after that, it continues producing ASM for the rest of the program using AT&T syntax. But we’ve changed the syntax in our inlined ASM and then gas will just emit errors whenever it finds AT&T syntax… which will just happen after our injected ASM. So, don’t do that. You better learn AT&T syntax, that will be very useful, or use the `-masm=intel` gcc flag instead, which will force gcc to produce the asm using Intel syntax and call gas accordingly.

We’ve just discussed Intel processors so far, but the concept works on almost any architecture out there, but using different instructions. This is a small table with the equivalent breakpoint instructions for the more popular architectures.

| Architecture | Instruction | Machine Code |
| ------------ | ----------- | ------------ |
| x86          | `INT3`      | 0xCC         |
| ARM (A32)    | `BKPT #0`   | 0xE1200070   |
| ARM (Thumb)  | `BKPT #0`   | 0xBE00       |
| ARM (Thumb2) | `BKPT #0`   | 0xBE00       |
| AArch64      | `BRK #0`    | 0xd4200000   |
| MIPS         | `BREAK 0`   | 0x0000000D   |
| RISC-V       | `EBREAK`    | 0x00100073   |
| RSIC-V C-Ext | `C.EBREAK`  | 0x9002       |

Note that in the table above, with the exception of `x86`, all other platforms have a fixed instruction size, so even when the instruction is 16 or 32 bits long, it always fits perfectly in code memory. Intel having variable length instructions really needs to use a 1-byte instruction for setting up software breakpoints.

All the instructions in the table, when executed by the indicated processor, will produce a `SIGTRAP` as the `int 3` we discussed earlier in the section.

Instead of using the processor-specific instruction to produce a breakpoint, you can use the `raise` function that will send the signal passed as parameter to the current process:

```C
   raise (SIGTRAP);   // equivalent to kill(getpid(), SIGTRAP)
```

Note, however, that this will be delivered through the standard kernel signal delivery mechanism, while the processor interrupt will generate an interrupt in the kernel that will notify the process. Actually, if you see both of them using `strace`, you’ll immediately notice the difference:

    --- SIGTRAP {si_signo=SIGTRAP, si_code=SI_KERNEL, si_addr=NULL} ---                int3
    --- SIGTRAP {si_signo=SIGTRAP, si_code=SI_TKILL, si_pid=2060002, si_uid=1000} ---  raise (SIGTRAP)

As you can see, the `si_code` field clearly indicates how the signal was generated. Also note that using `raise` opens the door to hijack that function using our `LD_PRELOAD` trick.

Injection of software breakpoints can be used for more advanced techniques in which two processes work in parallel (one debugging the other) and the breakpoints are used as a way to give control to the traced process. We’ll implement this technique later in the book in different contexts and also we’ll see this technique live in the final project for this chapter.

### Integrity Checkers

You now know that setting a breakpoint in a program actually modifies it, so a program can look for breakpoint instructions or just do a checksum of its code to verify whether it has or hasn’t been modified by a debugger, which may have set breakpoints all over the place. As a side effect, such a memory check will also detect any attempt to patch the binary to skip some of those protections we’ve seen earlier in this chapter.

For doing that, the program needs to know where its code is. There are a few ways of doing that, but if you have control on the build of your application, the cleaner way to achieve it is using a linker script.

> **NOTE**
>
> You could rely on other symbols emitted by the linker like `_start` and `_fini` that usually match the values we need and in this case, they do, however, using our own symbols ensures that we get what we really want and it’s always cool to use a linker script.

Using a linker script you can create symbols at specific parts of the binary that can be used at run-time. Let’s see a simple example that adds a start and end symbols to indicate where the `.text` section is located in memory.

```
SECTIONS{
	_text_start = ADDR(.text);
	_text_end = ADDR(.text) + SIZEOF(.text);
}
```
This linker script creates two new symbols named `_text_start` and `_text_end` and initializes them with the values of the start and end of the `.text` section. Using this linker script will make these symbols available to our program, and what is even better, will make the linker calculate their values for us.

Using this linker script, we can easily write a program that checks its integrity using this method.

```C
#include <stdio.h>
#include <stdint.h>

uint64_t    cksum = 0x9d72; // Precalculated checksum

extern char _text_start[];  // Symbols filled by Linker Script
extern char _text_end[];

int text_checksum () {
  uint64_t c = 0;
  for (uint8_t *p = (uint8_t*) _text_start;p < (uint8_t*)_text_end; c += *p++);
  return c;
}

int main () {
  uint64_t c = text_checksum();
  printf ("%0x\n", c);
  if (c != cksum) {
    printf ("Code was modified\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE:** Integrity checking
>
> _Antidebugging_
>
> Perfrom a checksum/hash of sensitive program areas to detect modifications like patching or breakpoints.


In order to compile this code using the linker script introduced earlier, you can use the following command:

    $ gcc -g -o integrity integrity.c -Wl,integrity.ld

Where `integrity.ld` is the name I gave to my linker script. The `-Wl,` flag allows us to provide parameters to the linker, in this case, the linker script.

A way to get the checksum value is to add a `printf` in our test program, after all we’re already calculating it, however, we don’t want that code in the final program. So, a possible solution is to create a separate program that calculates the checksum. Note that modifying the checksum value is not a problem because that lives on the `.data` section, and changes to that section won't affect the `.text`/code checksum. So, let's write a tool to calculate the `.text` section checksum:

```C
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

int text_checksum (int fd, long start, ssize_t size) {
  uint64_t  c = 0;
  int       n = size;
  uint8_t  *buffer = malloc (size);

  // Read data from file
  lseek (fd, start, SEEK_SET);
  n = read (fd, buffer, size);
  if (n != size) printf("Not completely read\n");
  for (int i = 0; i < size; c += buffer[i++]);
  free (buffer);
  return c;
}

int main (int argc, char *argv[]) {
  if (argc != 4) {
    fprintf (stderr, "usage: %s start size binary\n");
    exit (EXIT_FAILURE);
  }
  int fd = open (argv[3], O_RDONLY);
  uint64_t c = text_checksum(fd, atol (argv[1]), atol(argv[2]));
  printf ("Checksum: %0x\n", c);
  close (fd);
  return 0;
}
```

This program just reads the part of the binary containing the code and calculates the checksum. I’ve kept it intentionally simple, and instead of parsing the ELF sections to figure out where the code is, I just pass the file offset and size to use for calculating the checksum. This way, we can also use the same program to calculate the checksum of any part of the file. The negative part is that we need to get the initial offset and size of the `.text` section by ourselves. Fortunately, this can be done very easily using `readelf`:

    $ readelf -S test5 | grep -A1 ".text"
      [ 1] .text             PROGBITS         0000000000001050  00001050
           000000000000017c  0000000000000000  AX       0     0     16

In this case, the file offset will be `0x1050` and the size `0x17c`, but that may change in your box or every time you do a change on `test5`. That is the negative side of not parsing the program headers (or sections; we could use them in this case) in the program.

> **EXERCISE:**
>
> Update the `checksum` program to obtain the `.text` offset and size directly from the file, so you don’t need to run `readelf` before to obtain the right parameters. Extra points if you can specify the section you want to check sum.

Alternatively, we can use the `text_start` and `text_end` symbols created by our linker script, which may be more convenient in case we want to automate the whole process using a shell script instead of parsing the ELF structures:

    $ readelf -s test5 | grep text
        28: 00000000000011cc     0 NOTYPE  GLOBAL DEFAULT    1 _text_end
        32: 0000000000001050     0 NOTYPE  GLOBAL DEFAULT    1 _text_start
        37: 0000000000001139    66 FUNC    GLOBAL DEFAULT    1 text_checksum

Note that method one gives us `offset/start` and `size`, and method two gives us `start` and `end`. You can calculate the right values yourself or update the checksum program to use one or the other set of parameters. Both options are completely equivalent. With this data, we can now calculate the code checksum of our program.

    $ ./checksum $((0x00001050)) $((0x17c)) test5
    Checksum: 92fc

Once we get the checksum, we have to update the program so the variable `cksum` contains the value we’ve just calculated, and we’re good to try our new `integrity` program. Just load the program in `gdb`, set a breakpoint anywhere, for example in `main` (`b main`), and run the program. It will report that it was modified.

A real program won't do this check at the very beginning as we've done in our example, and it will likely do it many times under complicated conditions. For example, install a signal that checks integrity every 5 seconds and another every 10 seconds that checks the first is still working... However, keep in mind that it doesn't matter how much you mesh it up. It can always be undone, so the game is mainly about how long it takes the reverser to un-mesh the protection and if it’s worth the energy/cost. Furthermore, you don't want to calculate the checksum in a regular function; instead, declare the function as inline. This way, the code will be copied to the place where it’s used, and disabling the integrity check will require disabling each of the function calls. Using a regular function would allow the adversary to just patch that function to fool all checks at once.

Note that this technique, as I mentioned before, will also protect against patching. Whenever any byte on the file changes, the checksum will also do, unless the patching. In this case, the checksum is just a sum, so it’s very easy to adjust to account for any patching. If you’re serious about this, better use a proper hash function and try to make it hard to figure out where and when these checks are performed.

### Indirect Debugger Detection Techniques

Most anti-debugging techniques (the ones we’ll discuss here, but there are others) revolve around the detection of being debugged and, if so, stopping execution immediately. As you can imagine, this is half of the history, and these techniques are intended to mainly fool automatic tools, not humans doing a static analysis or a directed dynamic analysis. Anyhow, it’s good to discuss a few other alternatives that are often used by malware, or just programs that don’t want to make life easy for reversers (games for example).

The three main ways to determine if a program is being debugged are:

* Abuse the fact that a given program can only be traced by one and only one tracer.
* Use the `/proc` file system
* Use indirect indications of debugger presence

We’d already seen a few examples of the first one, but it’s worth mentioning another variation that consists of creating a child process and tracing it, so the child process can be traced, but then, the child process also starts tracing the parent, so none of them can be traced. This schema can be further complicated by distributing the operations between both processes and using the debugging infrastructure as part of those operations. We’ll see an example in the chapter project.

The use of the `/proc` file system is pretty straightforward; let’s quickly discuss it. One of the entries in the `/proc/pid` directory is `status`, which provides a lot of information about the status of the process. One of the fields in that file is `TracerPid:`, which contains the pid of the tracing process attached or 0 otherwise. So this technique consists of extracting that information from the file.

```C
#include <stdio.h>
#include <string.h>

int check_tracer () {
  FILE *f = fopen ("/proc/self/status", "r");
  char buffer[1024];
  int  r = 0;
  
  while (!feof(f)) {
    fgets (buffer, 1024, f);
    if (!strncmp(buffer, "TracerPid:", 10)) {
      sscanf (buffer + 11, "%d", &r);
      fclose (f);
      return r;
    }
  }
  fclose (f);
  return -1;
}

int main () {
  if (check_tracer()) {
    printf ("Debugger detected. Aborting\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE:** Tracer detection via `/proc`
>
> _Anti-debugging/Debugger Detection_
>
> Use `/proc/PID/status` to check the `TracerId` field. This field contains the tracer PID if the process is being traced.


#### ASLR Disabling

Most debuggers disable ASLR (Address Space Layout Randomization), the kernel feature that selects a random memory address to load programs every time we run them. This is because, during debugging, it’s very convenient to get your program loaded into the same address every time because you may want to put breakpoints at specific memory addresses or check variables, sometimes through debugging scripts. Most of the times you can use symbols, but when that is not possible and you need to use absolute pointers, disabling ASLR makes the whole process simpler.

This has the side effect of, every time you run your PIE program in a debugger, ASLR gets disabled, and the program is loaded in the default memory address, which for Linux 64 bits PIE is usually `0x555555554000`. Debuggers like `gdb` and `lldb` do this by default, so an easy way to determine if a program is running inside a debugger is to check if its code is in that address range.

The following code shows an example of how to use this technique:

```C
#include <stdio.h>
#include <stdint.h>

int main () {
  printf ("%p\n", main);
  if (((uint64_t)main & 0xfffffffffff00000) == 0x555555500000) {
    printf ("gdb or lldb detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE:** Debugger detection by ASLR Deactivation
>
> _Anti-debugging/Debugger Detection_
>
> Check address of code and verify it's not in the default range. Debuggers disable ASLR forcing program to be loaded at a fixed default address.


In this example, we just check the address of `main` in the default range of addresses. Depending on the size of the program, we may need to make our bit mask bigger or check `_start` or the beginning of the `.text` segment to be sure that our check range is OK.

#### Timing Checks

This technique consists of making the program check how long it takes to do some calculation or, more generally, how long it takes for the program to go from one instruction to another. If the time is too high, it means the program has been interrupted in between and it took longer to execute than it should. Overall, they work well for manual debugging when a person is stepping through the program, checking what is going on.

Below is an example of how to use this technique:

```C
#include <stdio.h>
#include <time.h>

int main () {
  time_t   t1, t2;

  t1 = time (NULL);
  puts ("Some code here to step over");
  puts ("Some more");
  t2 = time (NULL);
  if (t2 - t1 > 0) {
    printf ("Debugger detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE:** Debugger detection by timming checkings
>
> _Anti-debugging/Debugger Detection_
>
> Check time difference between two instructions in the code. A longer time than usual will suggest debugging.


If you load it on a debugger and add a breakpoint at `main` to start running the program step by step, it’d take several seconds to reach the second `time` and the check will fail.

However, using this technique to detect automatic tools like `strace` or just a simple debug script may be tricky, as the time difference in those cases will be quite small and also will vary with the hardware running the program. In those cases, you may need to use `gettimeofday` or `clock_gettime`, both provided by the `vdso` (_Virtual Dynamic Shared Object_), which ensures very quick access as no system call is involved in getting those values.

> **VDSO. Virtual Dynamic Shared Object**
>
> We’ll see `vsdo` many times when looking at the processes memory map. `vsdo` is basically a shared library that is automatically made available to processes in the system and that provides special versions of some kernel functions. The classical example is the `gettimeofday`, a system call that gives us the current time with nanoseconds precision. The kernel keeps that value updated in its memory, and the system call just copies it from kernel to user space. As this is a read-only operation, it would be much more efficient to just let the user space programs access that kernel value directly, avoiding all the system call mechanics and improving performance on applications that require frequent use of those system calls. You can get a list of the functions provided by `vdso` in the associated man page. For most architectures, it just provides `clock_gettime`, `gettimeofday`, and `time`. Intel processors also includes a `getcpu` function.

Code below shows a simple example of how to use `clock_gettime`:

```C
#include <time.h>
#include <stdio.h>

int main() {
    struct timespec start, end;
    
    // Highly optimized via vDSO; directly queries underlying hardware registers
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // --- Code to measure ---
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long nanoseconds = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    printf("Elapsed time: %ld ns\n", nanoseconds);
}
```

These functions provide nanosecond precision, but as we said before, fine-tuning those measurements may be tricky, and if you adjust the time too much, lead to false positives.

#### Parent Process Verification

Using `getppid` or the `/proc` file system, a process can check who is its parent. If the program was opened by `gdb` or run with `strace`, that will be easily detected. The `ppid` information is also available in the `/proc/pid/status` entry for the process; you can easily modify the code we wrote to detect a debugger using that file to also get the parent information.

A simpler version using `getppid` is shown below:

```C
#include <stdio.h>
#include <string.h>
#include <unistd.h>


int main () {
  char path[1024];
  FILE *f;
  snprintf (path, 1024, "/proc/%d/comm", getppid());
  if ((f = fopen (path, "rt")) == NULL) return -1;
  fgets (path, 1024, f);
  fclose (f);
  if (!strncmp (path, "gdb",3 ) || !strncmp (path, "lldb",4) ||
      !strncmp (path, "strace", 6)) {
    printf ("Debugger detected\n");
    return -1;
  }
  printf ("Programs run normally\n");

  return 0;
}
```

> **TECHNIQUE:** Debugger detection by Parent verification

> _Anti-debugging/Debugger Detection_

> Check if the process parent (or alternatively `TracerPid`) is on a blacklist of tools to be avoided.

You can add other debuggers or dynamic analysis tools to the program above to detect other tools as well. The program above uses `/proc/PID/comm` to get the command name associated with the indicated PID. We could also use `/proc/PID/cmdline`, which contains the actual command-line used to launch the program (including any parameters). Note that the `comm` entry can be easily changed using the `prctl` `PR_SET_NAME` command.

#### Other techniques

For completeness, let’s include in this section some more generic techniques just for completeness. The first one consists in trying to detect if a library has been preloaded with `LD_PRELOAD`. This is very simple; we just go through all the environment variables and look for `LD_PRELOAD`. Something like this:


```C
#include <stdio.h>
#include <string.h>

int main (int argc, char *argv[], char *env[]) {
  printf ("Press a Key\n");
  char **p;
  getchar ();
  for (p = env; *p != NULL; p++) {
    if (strstr(*p, "LD_PRELOAD")) {
      printf ("LD_PRELOAD detected:\n%s\n. Aborting\n", *p);
      return -1;
    }
  }
  printf ("Programs run normally\n");
  return 0;
}
```

> **TECHNIQUE:** `LD_PRELOAD` injection detection using env vars
>
> _Anti-debugging/Debugger Detection_
>
> Check environment variables looking for `LD_PRELOAD`.

In this example, we make use of the third `main` parameter, which contains the environment variables. You can alternatively use the external global variable `environ`, which should point to the same location. On the other hand, a debugger can easily manipulate the environment seen by an application and modify the environment variables seen by the application at runtime.

Another common technique is looking for debuggers in memory, not just in the `TracerId` field we discussed below. We’ve already seen how to spoof that value, but making the debugger disappear from the global process list isn't that straightforward. Therefore, a program intended to be run in a regular user program can try to detect common debuggers or analysis tools, which normally wouldn't be there. This is a simple example of how to do that:

```C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#define DIE(s) {perror(s); exit(1);}

int main () {
  DIR            *d;
  struct dirent *dir;
  char           path[1024];

  if ((d = opendir ("/proc")) == NULL) DIE ("opendir:");
  while ((dir = readdir (d)) != NULL) {
    snprintf (path, 1024, "/proc/%s/comm", dir->d_name);
    FILE *f;
    if ((f = fopen (path, "rt")) == NULL) continue;
    fgets (path, 1024, f);
    if (strstr (path, "gdb") || strstr (path, "lldb") || strstr (path, "strace")) {
      printf ("Debugger detected. Aborting\n");
      exit (1);
    }
    fclose (f);
  }
  closedir (d);
  puts ("Program runs normally");
}

```

> **TECHNIQUE:** Global process scanning
>
> _Anti-debugging/Debugger Detection_
>
> Scan all processes running on the system looking for debuggers, analysis tools, or missing programs that won't be available in containers or VMs but should exist in regular systems.

The program just scans the `/proc` directory and reads the file `/proc/XX/comm` that contains the command associated with the process with PID XX. Well, actually, we'll be opening a couple of folders not associated with processes, but in those cases, the `comm` file isn't there. If the name of the process is in the list, the program just stops.

You can extend these techniques to detect environment variables set on containers or VMs that aren't set on normal systems or processes that are normally in a system but not in a container. Sure, we can exploit existence or non-existence equally for this kind of detection. The same idea can be applied to other system elements like drivers or BIOS, which may be modified or special for containers or VMs. It’s up to you to find the relevant characteristic to check, but the way to implement it will likely be a variation of what we’ve seen so far.



As mentioned before, any of these techniques can be easily removed by patching the binary or skipping the checks manually (for example), so, in the last instance, those techniques are not really intended to avoid debugging. That’s actually not possible. They are intended to either make the debugging process a bit longer and annoying (at least until you get all the checks disabled) or to fool automated dynamic analysis tools that may just run the program and look for suspicious actions. If the program detects any of those tools or environments, it’ll just run normal code so the tool won’t flag it as a potential threat, while when run outside the secure environment it will actually execute its nefarious payload.

As you can see, `ptrace` is a pretty central system call related to debugging and antidebugging techniques. But it can do much more.


